# ***** POWER MONITOR *****


INSTRUCTIONS:
1) load msr module (for CPU readings)
    $ sudo modprobe msr

2) make sure you have nvidia-ml (should be in ....../cuda/lib) and is reachable


3) make


4) sudo ./powermon interval-ms


5) when terminating, it will display a summary of power and energy values.



NOTES:
- some CPUs are incompatible with msr readings.
- on some CPUs, the DRAM value is not reachable and will give 0 Watts.
- the CPU power value is for the whole chip. Currently (2020) msr rapl does not give
  per-core readings.
- This tool includes code extracts from two repositories
    - GPU PowerMonitor.cpp by Pamela-project https://github.com/pamela-project/slambench1
    - Rapl Monitor tool by kentcz https://github.com/kentcz/rapl-tools

