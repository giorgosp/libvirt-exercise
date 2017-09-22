#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <libvirt/libvirt.h>

#define MAX_UNUSED_MEM_THRESHOLD 280 * 1024 // 280 Mib
#define MIN_UNUSED_MEM_THRESHOLD 180 * 1024 // 180 Mib

void printDomainMemoryStats(virDomainMemoryStatPtr stats, int size)
{
    char *tagStr;
    for (int i = 0; i < size; i++)
    {
        switch (stats[i].tag)
        {
        case VIR_DOMAIN_MEMORY_STAT_SWAP_IN:
            tagStr = "swap_in";
            break;
        case VIR_DOMAIN_MEMORY_STAT_SWAP_OUT:
            tagStr = "swap_out";
            break;
        case VIR_DOMAIN_MEMORY_STAT_MAJOR_FAULT:
            tagStr = "major_fault";
            break;
        case VIR_DOMAIN_MEMORY_STAT_MINOR_FAULT:
            tagStr = "minor_fault";
            break;
        case VIR_DOMAIN_MEMORY_STAT_UNUSED:
            tagStr = "unused";
            break;
        case VIR_DOMAIN_MEMORY_STAT_AVAILABLE:
            tagStr = "available";
            break;
        case VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON:
            tagStr = "balloon";
            break;
        case VIR_DOMAIN_MEMORY_STAT_RSS:
            tagStr = "rss";
            break;
        case VIR_DOMAIN_MEMORY_STAT_NR:
            tagStr = "stat_nr";
            break;
        default:
            assert(0);
        }

        printf("%s: %llu\n", tagStr, stats[i].val);
    }
}

unsigned long long domainMemoryStatsGetKiB(virDomainMemoryStatPtr stats, int nstats, int field)
{
    for (int i = 0; i < nstats; i++)
        if (stats[i].tag == field)
            return stats[i].val * 0.9765625; // convert from KB to KiB
    assert(0);
}

int main(int argc, char **argv)
{
    int interval = 0;
    if (argc > 1)
        interval = atoi(argv[1]);

    printf("interval %d\n", interval);

    if (interval == 0)
    {
        fprintf(stderr, "Please specify an integer interval.\n");
        return 1;
    }

    virConnectPtr conn;
    conn = virConnectOpen("qemu:///system");
    if (conn == NULL)
    {
        fprintf(stderr, "Failed to open connection to qemu:///system\n");
        return 1;
    }

    virDomainPtr *domains;
    int domainsLen = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);
    if (domainsLen < 0)
    {
        fprintf(stderr, "Failed to get domain list\n");
        return 1;
    }

    if (domainsLen == 0)
    {
        fprintf(stderr, "There are no domains running\n");
        return 1;
    }

    for (int i = 0; i < domainsLen; i++)
        assert(virDomainSetMemoryStatsPeriod(domains[i], 1, VIR_DOMAIN_AFFECT_CURRENT) == 0);

    // Assume all domains have the same max memory
    unsigned long maxMem = virDomainGetMaxMemory(domains[0]);
    printf("maxmem %d: %lu\n", 0, maxMem);

    virDomainMemoryStatPtr stats = malloc(sizeof(virDomainMemoryStatStruct) * (VIR_DOMAIN_MEMORY_STAT_NR + 1));
    int statsLen;

    unsigned long long unusedMem;

    while (1)
    {
        sleep(interval);

        // Check all domains in order to take back memory wherever the free(unused) memory
        // in the domain is above the max unused memory threshold
        for (int i = 0; i < domainsLen; i++)
        {
            memset(stats, 0, sizeof(virDomainMemoryStatStruct) * (VIR_DOMAIN_MEMORY_STAT_NR + 1));
            statsLen = virDomainMemoryStats(domains[i], stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
            assert(statsLen > 0);

            unusedMem = domainMemoryStatsGetKiB(stats, statsLen, VIR_DOMAIN_MEMORY_STAT_UNUSED);
            if (unusedMem > MAX_UNUSED_MEM_THRESHOLD)
            {
                // Take half of the unused memory or until at least MAX_UNUSED_MEM_THRESHOLD is left
                unsigned long long take = unusedMem / 2;
                unsigned long long allMem = domainMemoryStatsGetKiB(stats, statsLen, VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON);

                if (unusedMem - take < MAX_UNUSED_MEM_THRESHOLD)
                    take = unusedMem - MAX_UNUSED_MEM_THRESHOLD;

                unsigned long long newMem = allMem - take;

                printf("Take mem from %s %d\n", virDomainGetName(domains[i]), i);
                assert(virDomainSetMemory(domains[i], newMem) == 0);
            }
        }

        // Check all domains in order to give memory to whichever domain has less than
        // a threshold of unused memory
        for (int i = 0; i < domainsLen; i++)
        {
            memset(stats, 0, sizeof(virDomainMemoryStatStruct) * (VIR_DOMAIN_MEMORY_STAT_NR + 1));
            statsLen = virDomainMemoryStats(domains[i], stats, VIR_DOMAIN_MEMORY_STAT_NR, 0);
            assert(statsLen > 0);
            unusedMem = domainMemoryStatsGetKiB(stats, statsLen, VIR_DOMAIN_MEMORY_STAT_UNUSED);

            if (unusedMem < MIN_UNUSED_MEM_THRESHOLD)
            {
                // Give the domain 50% more memory than it has now
                unsigned long long allMem = domainMemoryStatsGetKiB(stats, statsLen, VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON);
                unsigned long long newMem = allMem + allMem * 0.5;
                if (newMem > maxMem)
                    newMem = maxMem;
                printf("Give mem to %s %d\n", virDomainGetName(domains[i]), i);
                assert(virDomainSetMemory(domains[i], newMem) == 0);
            }
        }

        // int kiB = 1024;
        // int memory = 100 * kiB;
        // assert(virDomainSetMemory(domains[1], memory) == 0);

        printf("Host free mem: %llu\n", virNodeGetFreeMemory(conn));
    }
}