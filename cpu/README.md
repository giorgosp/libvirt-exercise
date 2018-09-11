# vCpu scheduler

An exercise that assigns the VMs to physical CPUs so that all physical CPUs
are balanced.

## Compiling and running

Just run `make` and then `./vcpu_scheduler [interval]`

## Implementation

First, the scheduler checks the physical cpus' usages in order to determine if they are balanced. If not,
then the scheduler pins the vCpus according to their usage. The pCpus' usages are not taken into account
during pinning. The scheduler makes the assumption that at the start of the scheduling algorithm each pCpu usage is 0%. Then, it pins the busiest vCpu to the pCpu with the less usage.  When a vCpu is pinned (in the scheduler's mind, real pinning doesn't happen yet)  to a pCpu, the vCpu's usage is added to the theoretical pCpu usage. When all vCpus are pinned in the scheduler's algorithm, the vcpu-pcpu maps are sent to libvirt to set the affinity to the system.

The scheduler does nothing on a run if the pCpus are balanced. To determine that pCpus are balanced, all pCpu usages should be similar and not differ by more than **3%**. However, it's very easy for the pCpus to
differ for more than 3%, and still be balanced. A better error margin would be 5% or even maybe 10%.



## Assumptions made
- Vms should be already running when the scheduler starts.
- Each virtual machine is supposed to have only 1 vCpu.
- While the scheduler is running, the number of vms and the number of online physical cpus stays the same.
