#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clocksource.h>
#include <linux/kprobes.h>

// Module metadata
MODULE_AUTHOR("Rafał Radtke rafradek@hotmail.com");
MODULE_DESCRIPTION("Kernel module for AMD CPU BCLK overclocking");
MODULE_LICENSE("Dual MIT/GPL");

#ifndef KBUILD_MODNAME
    #define KBUILD_MODNAME "zen-bclk-oc"
#endif

#define PCI_DEVICE_ID_AMD_17H_ROOT         0x1450
#define PCI_DEVICE_ID_AMD_17H_M10H_ROOT    0x15d0
#define PCI_DEVICE_ID_AMD_17H_M60H_ROOT    0x1630
#define PCI_DEVICE_ID_AMD_17H_M30H_ROOT    0x1480

#define MSR_AMD64_MPERF_READONLY    0xC00000E7
#define MSR_AMD64_APERF_READONLY    0xC00000E8
#define MSR_AMD64_P0STATE           0xC0010064
#define MSR_AMD64_CPPC_REQ          0xC00102B3
#define MSR_AMD64_PP0_ENERGY_STATUS	0xC001029A
#define MSR_AMD64_PKG_ENERGY_STATUS	0xC001029B
#define MSR_AMD64_RAPL_POWER_UNIT   0xC0010299
#define MSR_AMD_F17H_HW_PSTATE_STATUS		0xC0010293

#define F17H_M01H_REPORTED_TEMP_CTRL        0x00059800

#define F17H_M01H_SVI                       0x0005A000
#define F17H_M70H_SVI_TEL_PLANE0            (F17H_M01H_SVI + 0x10)
#define F17H_M70H_SVI_TEL_PLANE1            (F17H_M01H_SVI + 0xC)

#define F17H_TEMP_ADJUST_MASK               0x80000

#define AMD_FCH_MMIO_BASE		0xFED80000
#define AMD_FCH_REFCLK_BASE		0xE00
#define AMD_FCH_REFCLK_SIZE		0x48

#define REFCLK_MIN_KHZ 96000
#define REFCLK_MAX_KHZ 151000

static const struct resource amd_fch_refclk_iores =
	DEFINE_RES_MEM_NAMED(
		AMD_FCH_MMIO_BASE + AMD_FCH_REFCLK_BASE,
		AMD_FCH_REFCLK_SIZE,
		"amd-fch-refclk-iomem");

void __iomem *amd_fch_refclk_base_ptr;
u32 __iomem *amd_fch_refclk_ssc_ptr;
u32 __iomem *amd_fch_refclk_clk_ptr;
u32 __iomem *amd_fch_refclk_loaden_ptr;

struct platform_device *device = NULL;

char *saved_command_line = NULL;
struct clocksource *clocksource_hpet;
struct clocksource *clocksource_tsc;

static u64 tsc_mult_init = 0;
static int cpu_khz_init = 0;
static int tsc_khz_init = 0;
static u64 loops_per_jiffy_init = 0;

typedef void *kallsyms_lookup_name_f(const char *);

typedef int change_clocksource_f(void *);
change_clocksource_f *change_clocksource;

static void *lookup_function_address(const char *func_name) {
    static void *kallsyms_addr = NULL;
    static kallsyms_lookup_name_f *kallsyms_func;
    if (kallsyms_addr == NULL) {
        struct kprobe kp;
        memset(&kp, 0, sizeof(struct kprobe));
        kp.symbol_name = "kallsyms_lookup_name";
        if (register_kprobe(&kp) < 0) {
            pr_info_once("cannot lookup %s, kprobe not available\n", func_name);
            return NULL;
        }
        kallsyms_addr = (void *)kp.addr;
        kallsyms_func = (kallsyms_lookup_name_f *)kallsyms_addr;
        unregister_kprobe(&kp);
    }
    if (kallsyms_addr == NULL) {
        pr_info_once("cannot lookup %s, kallsyms not available\n", func_name);
        return NULL;
    }
    return kallsyms_func(func_name);
}

static int refclk_get(void) {
    u32 clk = ioread32(amd_fch_refclk_clk_ptr); 
    int frac = (((clk >> 25) & 0xF) * 1000 + 15) / 16;
    int clk_off = (clk >> 4) & 0xFF;
    if ((clk_off & 4) != 0) {
        clk_off -= 8;
    }
    return (clk_off + 100) * 1000 + frac;
}
volatile int current_refclk = 100000;
int target_refclk = 0;
bool ssc = false;

static void refclk_set(int clk_khz) {
    int clk_mhz = clk_khz / 1000;
    int clk_frac = (clk_khz % 1000) * 16 / 1000;
    int clk_off = (clk_mhz - 100);
    if ((clk_off & 4) != 0) {
        clk_off += 8;
    }
    current_refclk = clk_khz;

    iowrite32((ioread32(amd_fch_refclk_loaden_ptr) | 0x2000000), amd_fch_refclk_loaden_ptr);
    iowrite32((ioread32(amd_fch_refclk_clk_ptr) & 0xE1FFF000) | (clk_off << 4) | ((clk_frac & 0xF) << 25), amd_fch_refclk_clk_ptr);
    iowrite32((ioread32(amd_fch_refclk_loaden_ptr) | 0x40000000), amd_fch_refclk_loaden_ptr);

    clocksource_tsc->mult = tsc_mult_init * 100000 / clk_khz;
    clocksource_tsc->cs_last = clocksource_tsc->read(clocksource_tsc);
    clocksource_tsc->wd_last = clocksource_hpet->read(clocksource_hpet);
    change_clocksource(clocksource_tsc);
    cpu_khz = (u64)cpu_khz_init * clk_khz / 100000;
    tsc_khz = (u64)tsc_khz_init * clk_khz / 100000;
    loops_per_jiffy = loops_per_jiffy_init * clk_khz / 100000;
}

DEFINE_MUTEX(refclk_set_mutex);

static bool refclk_set_target(int clk) {
    if (clk < REFCLK_MIN_KHZ || clk > REFCLK_MAX_KHZ) {
        pr_warn("BCLK %d not in [%d, %d] range\n", clk, REFCLK_MIN_KHZ, REFCLK_MAX_KHZ); 
        return false;
    }
    mutex_lock(&refclk_set_mutex);
    int prev_delay_clk = current_refclk;
    pr_info("Setting BCLK to %d kHz\n", clk); 
    while (clk > current_refclk) {
        refclk_set(min(current_refclk + 125, clk));
        udelay(50);
        // Additional delay if the difference in clock is too high
        if (current_refclk - prev_delay_clk > 1000) {
            prev_delay_clk = current_refclk;
            msleep(200);
        }
    }
    while (clk < current_refclk) {; 
        refclk_set(max(current_refclk - 250, clk));
        udelay(50);
        // Additional delay if the difference in clock is too high
        if (prev_delay_clk - current_refclk > 2000) {
            prev_delay_clk = current_refclk;
            msleep(200);
        }
    }
    mutex_unlock(&refclk_set_mutex);
    pr_info("BCLK changed\n"); 
    return true;
}

static ssize_t bclk_khz_show(struct device *dev, struct device_attribute *attr,
			char *buf) {
    
    ssize_t ret = sprintf(buf, "%d\n", refclk_get());
    return ret;
}

static ssize_t bclk_khz_store(struct device *kobj, struct device_attribute *attr,
			 const char *buf, size_t count) {

    int clk = 0;
    if (sscanf(buf,"%d", &clk) == 1) {
        refclk_set_target(clk);
    }
    return count;
}

DEVICE_ATTR_RW(bclk_khz);

static int zen_bclk_oc_probe(struct platform_device *dev) {
    device = dev;

    clocksource_hpet = lookup_function_address("clocksource_hpet");
    clocksource_tsc = lookup_function_address("clocksource_tsc");
    change_clocksource = lookup_function_address("change_clocksource");
    saved_command_line = *(char **)lookup_function_address("saved_command_line");

    if (!clocksource_hpet || !clocksource_tsc || !change_clocksource) {
        pr_err("Clocksource objects not available\n");
        return -ENOENT;
    }

    amd_fch_refclk_base_ptr = devm_ioremap_resource(&device->dev, &amd_fch_refclk_iores);

    if (!amd_fch_refclk_base_ptr) {
        pr_err("Failed to map AMD FCH memory\n");
        return -ENXIO;
    }

    amd_fch_refclk_ssc_ptr = amd_fch_refclk_base_ptr + 0x08;
    amd_fch_refclk_clk_ptr = amd_fch_refclk_base_ptr + 0x10;
    amd_fch_refclk_loaden_ptr = amd_fch_refclk_base_ptr + 0x40;

    iowrite32((ioread32(amd_fch_refclk_ssc_ptr) & 0xFFFFFFFE) | (ssc ? 1 : 0), amd_fch_refclk_ssc_ptr);

    pr_info("BCLK spread spectrum is %s\n", ssc ? "enabled" : "disabled"); 

    tsc_mult_init = clocksource_tsc->mult;
    cpu_khz_init = cpu_khz;
    tsc_khz_init = tsc_khz;
    loops_per_jiffy_init = loops_per_jiffy;
    current_refclk = refclk_get();
    if (current_refclk != 100000) {
        tsc_mult_init = tsc_mult_init * current_refclk / 100000;
        cpu_khz_init = (u64)cpu_khz_init * 100000 / current_refclk;
        tsc_khz_init = (u64)tsc_khz_init * 100000 / current_refclk;
        loops_per_jiffy_init = loops_per_jiffy_init * 100000 / current_refclk;
    }
    clocksource_tsc->cs_last = clocksource_tsc->read(clocksource_tsc);
    clocksource_tsc->wd_last = clocksource_hpet->read(clocksource_hpet);

    if (target_refclk != 0 && !(saved_command_line && strstr(saved_command_line, " recovery "))) {
        bool success = refclk_set_target(target_refclk);
        if (!success) {
            return -EINVAL;
        }
    }

    device_create_file(&dev->dev, &dev_attr_bclk_khz);
    
    return 0;
}

static int zen_bclk_oc_remove(struct platform_device *dev) {

    device_remove_file(&dev->dev, &dev_attr_bclk_khz);
    refclk_set_target(100000);

    udelay(500);
    return 0;
}
int pre_suspend_refclk = 100000;
static int zen_bclk_oc_suspend(struct device *dev) {
    pre_suspend_refclk = current_refclk;
    refclk_set_target(100000);
    return 0;
}

static int zen_bclk_oc_resume(struct device *dev) {
    refclk_set_target(pre_suspend_refclk);
    return 0;
}

DEFINE_SIMPLE_DEV_PM_OPS(pm_ops, zen_bclk_oc_suspend, zen_bclk_oc_resume);

static struct platform_driver zen_bclk_oc = {
    .remove = zen_bclk_oc_remove,
    .probe = zen_bclk_oc_probe,
    .driver = {
        .name = "zen-bclk-oc",
        .pm = &pm_ops
    }
};

static int __init zen_bclk_oc_init(void) {
    
    if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD) {
        pr_err("CPU Vendor is not AMD\n");
        return -ENODEV;
    }
    
    int retval = platform_driver_register(&zen_bclk_oc);
    if (retval) {
        pr_err("Failed to register the driver\n");
        return retval;
    }
    struct platform_device *pdev = platform_device_alloc("zen-bclk-oc", -1);
    if (!pdev) {
        return -ENOMEM;
    }
    
    retval = platform_device_add(pdev);
    if (retval) {
        platform_device_put(pdev);
        return retval;
    }
    pr_info("Initialized\n");
    return 0;
}

static void zen_bclk_oc_exit(void) {
    platform_device_unregister(device);
    platform_driver_unregister(&zen_bclk_oc);
}

module_init(zen_bclk_oc_init);
module_exit(zen_bclk_oc_exit);

module_param_named(bclk_khz, target_refclk, int, 0644);
module_param_named(ssc, ssc, bool, 0644);