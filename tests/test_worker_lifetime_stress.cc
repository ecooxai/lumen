/* Repeated real Lumen worker lifetime validation, using the frozen native
 * lifecycle fixture unchanged. This test does not constitute GUI scrolling,
 * iOS device, or IPA acceptance. */
#define main worker_async_single_run_main
#include "test_worker_async_lifecycle.cc"
#undef main
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fstream>
#ifdef __APPLE__
#include <libproc.h>
#endif

static int journal_fd = -1;
static unsigned journal_cycle = 0;
static void journal(const char *phase, const char *name) {
    if (journal_fd < 0) return;
    const int saved_errno = errno;
    const int stdout_fd = fcntl(STDOUT_FILENO, F_GETFD);
    const int stderr_fd = fcntl(STDERR_FILENO, F_GETFD);
    char line[768];
    const int length = snprintf(line, sizeof line,
        "{\"cycle\":%u,\"phase\":\"%s\",\"case\":\"%s\",\"checks\":%d,\"failures\":%d,\"stdout_error\":%d,\"stderr_error\":%d,\"stdout_fd\":%d,\"stderr_fd\":%d,\"pid\":%d}\n",
        journal_cycle, phase, name, checks, failures, ferror(stdout), ferror(stderr), stdout_fd, stderr_fd, getpid());
    if (length > 0 && static_cast<size_t>(length) < sizeof line) (void)write(journal_fd, line, static_cast<size_t>(length));
    errno = saved_errno;
}
static void journal_at_exit() {
    journal("atexit", "process");
    if (journal_fd >= 0) close(journal_fd);
}
static void run_case(const char *name, void (*function)()) {
    journal("before", name);
    function();
    journal("after", name);
}

struct ResourceCheckpoint {
    long rss_kib = -1, threads = -1, fds = -1;
};

static ResourceCheckpoint resource_checkpoint(unsigned cycle, bool emit = true) {
    ResourceCheckpoint resource;
#ifdef __APPLE__
    proc_taskinfo task{};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task, sizeof task) == sizeof task) {
        resource.rss_kib = static_cast<long>(task.pti_resident_size / 1024);
        resource.threads = task.pti_threadnum;
    }
    const int capacity = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (capacity >= 0) {
        std::vector<proc_fdinfo> descriptors(static_cast<size_t>(capacity) / sizeof(proc_fdinfo) + 8);
        const int size = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, descriptors.data(),
                                     static_cast<int>(descriptors.size() * sizeof(proc_fdinfo)));
        if (size >= 0) resource.fds = size / sizeof(proc_fdinfo);
    }
#else
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) resource.rss_kib = strtol(line.c_str() + 6, nullptr, 10);
        if (line.rfind("Threads:", 0) == 0) resource.threads = strtol(line.c_str() + 8, nullptr, 10);
    }
    DIR *directory = opendir("/proc/self/fd");
    if (directory) {
        resource.fds = -1; // Exclude this directory's own temporary descriptor.
        while (dirent *entry = readdir(directory)) if (entry->d_name[0] != '.') ++resource.fds;
        closedir(directory);
    }
#endif
    if (emit) {
        printf("RESOURCE_CHECKPOINT {\"cycle\":%u,\"rss_kib\":%ld,\"threads\":%ld,\"fds\":%ld,\"pending\":%d,\"workers\":%zu}\n",
               cycle, resource.rss_kib, resource.threads, resource.fds, net_pending(), g_workers.size());
        fflush(stdout);
    }
    return resource;
}

int main(int argc, char **argv) {
    unsigned cycles = argc > 1 ? static_cast<unsigned>(strtoul(argv[1], nullptr, 10)) : 20;
    if (cycles < 5 || cycles > 100) { fprintf(stderr, "cycles must be from 5 to 100\n"); return 2; }
    if (argc > 2) {
        journal_fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (journal_fd < 0) { perror("stress journal"); return 2; }
        if (std::atexit(journal_at_exit)) return 2;
    }
    dom_init(); net_init(4); font_init(); js_global_init(argv[0]);
    ResourceCheckpoint warm;
    for (unsigned cycle = 1; cycle <= cycles; ++cycle) {
        journal_cycle = cycle;
        printf("STRESS_CYCLE %u of %u\n", cycle, cycles); fflush(stdout);
        run_case("concurrency", check_concurrency);
        run_case("fetch_abort", check_fetch_abort);
        run_case("completion_burst", check_fetch_completion_burst);
        run_case("falsy_abort_reasons", check_falsy_abort_reasons);
        run_case("owner_loop_independence", check_owner_loop_independence);
        run_case("xhr_reopen", check_xhr_reopen);
        run_case("xhr_timeout_and_sync", check_xhr_timeout_and_sync);
        run_case("xhr_reentrant_open", check_xhr_reentrant_open);
        run_case("terminate", [] { check_exit_cancellation(false); });
        run_case("self_close", [] { check_exit_cancellation(true); });
        CHECK("cycle leaves no registered workers", g_workers.empty());
        CHECK("cycle leaves no pending network ownership", net_pending() == 0);
        ResourceCheckpoint current = resource_checkpoint(cycle, false);
        if (cycle > 4 && (current.threads > warm.threads || current.fds > warm.fds)) {
            // A detached worker releases its last Lumen ownership and signals
            // completion before pthread return/TLS teardown is visible to the
            // kernel. Preserve that initial observation, then bound settling.
            ResourceCheckpoint initial = current;
            const auto begun = std::chrono::steady_clock::now();
            const auto deadline = begun + std::chrono::milliseconds(500);
            while ((current.threads > warm.threads || current.fds > warm.fds)
                   && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                current = resource_checkpoint(cycle, false);
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - begun).count();
            printf("RESOURCE_SETTLE {\"cycle\":%u,\"initial_threads\":%ld,\"initial_fds\":%ld,\"final_threads\":%ld,\"final_fds\":%ld,\"elapsed_ms\":%lld,\"limit_ms\":500}\n",
                   cycle, initial.threads, initial.fds, current.threads, current.fds,
                   static_cast<long long>(elapsed));
        }
        current = resource_checkpoint(cycle);
        CHECK("native resource checkpoint is readable", current.rss_kib > 0 && current.threads > 0 && current.fds >= 0);
        if (cycle == 4) warm = current;
        if (cycle > 4) {
            CHECK("settled file descriptor count does not accumulate", current.fds <= warm.fds);
            CHECK("settled native thread count does not accumulate", current.threads <= warm.threads);
        }
        if (failures) break;
    }
    CHECK("stress workers complete before process shutdown", js_workers_shutdown(3000));
    CHECK("stress requests finalize before process shutdown", drain_network());
    journal("idle_begin", "process");
    printf("STRESS_IDLE_BEGIN\n"); fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(12));
    resource_checkpoint(cycles + 1);
    net_shutdown();
    printf("RESULTS worker-lifetime-stress %s (%d/%d checks)\n", failures ? "FAIL" : "PASS", checks - failures, checks);
    fflush(stdout);
    journal("return", "process");
    return failures ? 1 : 0;
}
