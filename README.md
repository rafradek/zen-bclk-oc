# zen-bclk-oc
Linux kernel module for overclocking BCLK on AMD Ryzen CPUs

# Disclaimer
Use at your own risk. BCLK not only alters CPU frequency, but also affects memory, drives (potentially leaving corrupted data on disk) and PCIE devices
A bad overclock may persist after a shutdown, in which case it might be necessary to unplug PC from AC for 10-15 seconds to reset BCLK.

# Compatibility
All desktop and mobile CPUs using Zen 1-3 architecture should be supported, including CPUs with locked overclocking.

Tested on:
- Ryzen 5 5600H
- Ryzen 5 5600