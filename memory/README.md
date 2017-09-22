# vCpu scheduler

## Compiling and running

Just run `make` and then `./memory_coordinator [interval]`

## Implementation

The memory coordinator checks that the unused memory of a domain is always
between two boundaries. If the unused memory is lower than a threshold, then 
the program gives 50% more memory to that domain. If the unused memory is more
than some other threshold, then the program takes memory from that domain until the unused memory reaches that threshold (maximum allowed unused memory). Between the two threshold, the domain's memory is not changed by the program.


## Test setup
To run the tests, 4 vms where used. For my tests (and log output), the  programs's interval is at 3 seconds. I believe for the memory coordinator, the smaller the interval the better for the jobs running, because the coordinator can give quickly more memory as it is needed. If the coordinator runs much less often, then maybe the jobs will ask for more memory earlier than the coordinator can run and they will not reach the max memory for a domain.


## Assumptions made
There are some assumptions made for the test environment, as per the project's description and for the ease of testing. For example:

- Vms should be already running when the program starts.
- All domains have the same max memory
