#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <malloc/malloc.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

bool platform_stats(PlatformStats *stats) {
    if (!stats) return false;
    memset(stats, 0, sizeof *stats);
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage)) return false;
    stats->cpu_us = (uint64_t)usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec
        + (uint64_t)usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;
#ifdef __APPLE__
    task_vm_info_data_t memory; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&memory, &count) == KERN_SUCCESS)
        stats->rss_bytes = memory.phys_footprint;
    else stats->rss_bytes = usage.ru_maxrss;
    mach_port_t host = mach_host_self();
    host_cpu_load_info_data_t cpu; count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(host, HOST_CPU_LOAD_INFO, (host_info_t)&cpu, &count) == KERN_SUCCESS) {
        stats->host_busy_ticks = (uint64_t)cpu.cpu_ticks[CPU_STATE_USER] + cpu.cpu_ticks[CPU_STATE_SYSTEM] + cpu.cpu_ticks[CPU_STATE_NICE];
        stats->host_total_ticks = stats->host_busy_ticks + cpu.cpu_ticks[CPU_STATE_IDLE];
    }
    vm_statistics64_data_t vm; count = HOST_VM_INFO64_COUNT; vm_size_t page_size = 0;
    if (host_page_size(host, &page_size) == KERN_SUCCESS && host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm, &count) == KERN_SUCCESS)
        stats->host_used_bytes = ((uint64_t)vm.internal_page_count - vm.purgeable_count + vm.wire_count + vm.compressor_page_count) * page_size;
    mach_port_deallocate(mach_task_self(), host);
#else
    stats->rss_bytes = (uint64_t)usage.ru_maxrss * 1024;
    FILE *file = fopen("/proc/self/statm", "r");
    unsigned long long pages;
    long page_size = sysconf(_SC_PAGESIZE);
    if (file) {
        if (fscanf(file, "%*s %llu", &pages) == 1 && page_size > 0) stats->rss_bytes = pages * (uint64_t)page_size;
        fclose(file);
    }
    file = fopen("/proc/stat", "r");
    if (file) {
        unsigned long long user, nice, system, idle, wait, irq, softirq, steal;
        if (fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &wait, &irq, &softirq, &steal) == 8) {
            stats->host_busy_ticks = user + nice + system + irq + softirq + steal;
            stats->host_total_ticks = stats->host_busy_ticks + idle + wait;
        }
        fclose(file);
    }
    file = fopen("/proc/meminfo", "r");
    if (file) {
        char line[256]; unsigned long long total = 0, available = 0;
        bool have_available = false;
        while (fgets(line, sizeof line, file)) {
            if (sscanf(line, "MemTotal: %llu kB", &total) == 1) continue;
            if (sscanf(line, "MemAvailable: %llu kB", &available) == 1) have_available = true;
        }
        if (have_available && total >= available) stats->host_used_bytes = (total - available) * 1024;
        fclose(file);
    }
#endif
    return true;
}

uint64_t platform_heap_bytes(void) {
#ifdef __APPLE__
    malloc_statistics_t stats = {0};
    malloc_zone_statistics(NULL, &stats);
    return stats.size_in_use;
#elif defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 33)
    struct mallinfo2 stats = mallinfo2();
    return (uint64_t)stats.uordblks + stats.hblkhd;
#else
    return 0;
#endif
#else
    return 0;
#endif
}
