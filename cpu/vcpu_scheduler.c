#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <libvirt/libvirt.h>

#define NANOS_PER_SEC 1000000000L
#define BALANCE_OFFSET 3 // tolerate 3% difference in cpu usages

struct vcpu_info
{
    int cpuId;
    unsigned long long cpuUsage;
};

int vcpu_info_comparator(const void *v1, const void *v2)
{
    return ((struct vcpu_info *)v1)->cpuUsage > ((struct vcpu_info *)v2)->cpuUsage;
}

unsigned int getOnlinePCpus(virConnectPtr conn)
{
    unsigned int online;
    int pcpus = virNodeGetCPUMap(conn, NULL, &online, 0);
    // We don't currently take into account cpus getting online/offline
    assert(pcpus == online);
    return online;
    // printf("PCPUS: %d\n", pcpus);
    // printf("online: %d\n", online);
    // for (int i = 0; i < pcpus; i++)
    //     printf("CPU %d: %llu\n", i, samplePCpuTime(conn, i));
}

unsigned long long samplePCpuTime(virConnectPtr conn, int cpuNum)
{
    int nparams = 0;
    assert(virNodeGetCPUStats(conn, cpuNum, NULL, &nparams, 0) == 0 && nparams != 0);

    virNodeCPUStatsPtr params = malloc(sizeof(virNodeCPUStats) * nparams);
    memset(params, 0, sizeof(virNodeCPUStats) * nparams);
    assert(virNodeGetCPUStats(conn, cpuNum, params, &nparams, 0) == 0);

    // for (int i = 0; i < nparams; i++)
    //     printf("%s %llu\n", params[i].field, params[i].value);

    unsigned long long busy_time = 0;
    for (int i = 0; i < nparams; i++)
        if (strcmp(params[i].field, VIR_NODE_CPU_STATS_USER) == 0 || strcmp(params[i].field, VIR_NODE_CPU_STATS_KERNEL) == 0)
            busy_time += params[i].value;

    free(params);
    return busy_time;
}

unsigned long long *getPCpuTimes(virConnectPtr conn, unsigned int *npcpus)
{
    unsigned int pCpus = getOnlinePCpus(conn);
    unsigned long long *pcpuTimes = malloc(sizeof(pcpuTimes) * pCpus);
    for (int i = 0; i < pCpus; i++)
        pcpuTimes[i] = samplePCpuTime(conn, i);
    *npcpus = pCpus;
    return pcpuTimes;
}

unsigned long long *getVCpuTimes(virConnectPtr conn, unsigned int *nvcpus)
{
    virDomainPtr *domains;
    int ndomains = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_RUNNING);
    assert(ndomains > 0);

    unsigned long long *vcpuTimes = malloc(sizeof(vcpuTimes) * ndomains);

    virDomainInfoPtr domainInfo = malloc(sizeof(virDomainInfo));
    assert(domainInfo);
    for (int i = 0; i < ndomains; i++)
    {
        memset(domainInfo, 0, sizeof(virDomainInfo));
        virDomainGetInfo(domains[i], domainInfo);
        vcpuTimes[i] = domainInfo->cpuTime;

        // printf("Domain %d: %s\n", i, virDomainGetName(domains[i]));
        // printf("cpuTime: %llu\n", domainInfo->cpuTime);
        // printf("---------------------------\n");
    }

    free(domainInfo);
    free(domains);

    *nvcpus = ndomains; // assume 1 vcpu per domain(vm)
    return vcpuTimes;
}

unsigned *calcCpuUsagePercent(unsigned long long *pcpuTimesNew, unsigned long long *pcpuTimesOld, unsigned int npcpus, int interval)
{
    unsigned *usages = malloc(sizeof(unsigned) * npcpus);
    for (int i = 0; i < npcpus; i++)
    {
        unsigned percent = 100 * (pcpuTimesNew[i] - pcpuTimesOld[i]) / (double)(interval * NANOS_PER_SEC);
        usages[i] = percent;
    }
    return usages;
}

int areNumbersBalanced(unsigned *numbers, unsigned int npcpus)
{
    for (int i = 0; i < npcpus; i++)
        for (int j = i; j < npcpus; j++)
            if (abs(numbers[i] - numbers[j]) > BALANCE_OFFSET)
                return 0;
    return 1;
}

int findLowestUsage(unsigned *usages, unsigned int size)
{
    int minId = 0;
    for (int i = 1; i < size; i++)
        if (usages[i] < usages[minId])
            minId = i;
    return minId;
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

    // Open connection to hypervisor and get all domains (VMs)

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

    // Variables for vcpu/pcpu data
    unsigned int nvcpus = 0;
    unsigned long long *vcpuTimes = NULL;
    unsigned long long *vcpuTimesNew = NULL;
    unsigned *vCpuUsages = NULL;

    unsigned int npcpus = 0;
    unsigned long long *pcpuTimes = NULL;
    unsigned long long *pcpuTimesNew = NULL;
    unsigned *pCpuUsages = NULL;

    // Get pcpu/vcpu stats at beginning
    pcpuTimes = getPCpuTimes(conn, &npcpus);
    vcpuTimes = getVCpuTimes(conn, &nvcpus);

    // Run vcpu scheduler at intervals
    // Assume that number of pcpus/vcpus(vms) is the same between intervals

    while (1)
    {
        sleep(interval);

        // Get new pcpu/vcpu stats
        pcpuTimesNew = getPCpuTimes(conn, &npcpus);
        vcpuTimesNew = getVCpuTimes(conn, &nvcpus);

        pCpuUsages = calcCpuUsagePercent(pcpuTimesNew, pcpuTimes, npcpus, interval);
        vCpuUsages = calcCpuUsagePercent(vcpuTimesNew, vcpuTimes, nvcpus, interval);

        int pCpusAreBalanced = areNumbersBalanced(pCpuUsages, npcpus);

        for (int i = 0; i < npcpus; i++)
            printf("PCPU %d %d%%\n", i, pCpuUsages[i]);
        for (int i = 0; i < nvcpus; i++)
            printf("vCPU %d %d%%\n", i, vCpuUsages[i]);
        printf("Are Balanced: %d\n", pCpusAreBalanced);

        if (!pCpusAreBalanced)
        {
            printf("Balancing %d nvcpus\n", nvcpus);

            // Balance vcpus to pcpus
            // For all vcpus, Take the most used vcpu and put it to the least used pcpu

            // put all vcpus in an ordered queue (could better use a priority queue here)
            struct vcpu_info *vcpusQueue = malloc(sizeof(struct vcpu_info) * nvcpus);
            for (int i = 0; i < nvcpus; i++)
            {
                vcpusQueue[i].cpuId = i;
                vcpusQueue[i].cpuUsage = vCpuUsages[i];
            }
            qsort(vcpusQueue, nvcpus, sizeof(struct vcpu_info), vcpu_info_comparator);
            struct vcpu_info *vcpusQueueIndexPtr = vcpusQueue;

            unsigned char vcpumap[nvcpus][npcpus / 8];
            memset(vcpumap, 0, sizeof(unsigned char) * nvcpus * npcpus / 8);

            // pop each vcpu from the queue and pin it to the pcpu with lowest usage
            memset(pCpuUsages, 0, sizeof(unsigned) * npcpus);
            for (int i = 0; i < nvcpus; i++)
            {
                struct vcpu_info vcpu = *vcpusQueueIndexPtr;
                vcpusQueueIndexPtr++;

                int pcpuId = findLowestUsage(pCpuUsages, npcpus);
                printf("Lowest pCpu %d %d\n", pcpuId, pCpuUsages[pcpuId]);
                vcpumap[vcpu.cpuId][pcpuId / 8] ^= 1 << pcpuId;
                pCpuUsages[pcpuId] += vcpu.cpuUsage;
            }

            for (int i = 0; i < nvcpus; i++)
            {
                for (int j = 0; j < npcpus / 8; j++)
                    printf("%d, ", vcpumap[i][j]);
                printf("\n");
            }

            for (int i = 0; i < nvcpus; i++)
            {
                // assume each domain has one vcpu so vcpu 0 belongs to domains[0], etc
                // if ((unsigned int)vcpumap[i][0] > (unsigned int)0)
                printf("map %d %d \n", i, vcpumap[i][npcpus / 8]);
                assert(virDomainPinVcpu(domains[i], 0, vcpumap[i], npcpus / 8) == 0);
            }

            // free(vCpuUsages);
            free(vcpusQueue);
        }

        free(pCpuUsages);

        free(pcpuTimes);
        pcpuTimes = pcpuTimesNew;

        free(vcpuTimes);
        vcpuTimes = vcpuTimesNew;

        printf("===================\n");

    }

    free(pcpuTimes);
    free(vcpuTimes);
}