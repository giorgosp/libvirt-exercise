# Memory coordinator

An exercise that uses the ballooning technique in order to give or take memory
to virtual machines.

## Compiling and running

Just run `make` and then `./memory_coordinator [interval]`

## Implementation

The memory coordinator checks that the unused memory of a domain is always
between two boundaries. If the unused memory is lower than a threshold, then 
the memory coordinator gives 50% more memory to that domain. If the unused memory is more
than the max limit, then the memory coordinator takes memory from that domain until the unused memory reaches that threshold (maximum allowed unused memory). Between the lower and upper limit, the domain's memory is not changed by the memory coordinator.
The memory coordinator runs every 3 seconds. However the smaller the interval the more quickly the coordinator can give or take memory from the VMs.

## Assumptions made
- Vms should be already running when the program starts.
- All domains have the same max memory
