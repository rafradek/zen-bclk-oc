# zen-bclk-oc
Linux kernel module for overclocking BCLK on AMD Ryzen CPUs

# Disclaimer
Use at your own risk. BCLK not only alters CPU frequency, but also affects memory, drives (potentially leaving corrupted data on disk) and PCIE devices.

A bad overclock may persist after a shutdown, in which case it might be necessary to unplug PC from AC for 10-15 seconds to reset BCLK.

# Compatibility
All desktop and mobile CPUs using Zen 1-3 architecture should be supported, including CPUs with locked overclocking.

Tested on:
- Ryzen 5 5600H
- Ryzen 5 5600

# Installation
### Requirements
- dkms package
- kallsyms, kprobes enabled in kernel (usually are enabled by default)
- kernel build tools

### Steps
1. Clone this repository and enter the directory
2. Run `sudo make dkms-install` command and reboot

To test bclk oc at runtime, run `echo <bclk_khz> | sudo tee /sys/kernel/zen_oc_cpufreq/bclk_khz` command

To apply blck oc at next boot, run `sudo ./set-boot-oc.sh <bclk_khz>` command

bclk_khz is BCLK OC target in kHz, for default 100 MHz, type 100000. Allowed range - from 96000 to 151000
# Configuration
Module parameters:
- bclk_khz - Set initial BCLK OC applied on load
- ssc - Keep BCLK spread spectrum enabled. It is disabled by default

Sysfs:

/sys/devices/platform/zen-bclk-oc/bclk_khz - Changes BCLK at runtime. May cause drifting timers in currently running apps that use raw rdtsc to measure time
