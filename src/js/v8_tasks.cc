#include "v8_tasks.h"
#include <unordered_map>

namespace {
std::mutex registry_mutex;
std::unordered_map<v8::Isolate *, std::shared_ptr<JsForegroundTasks>> runners;

/* Keep libplatform's background compiler jobs. Page and worker foreground
 * queues notify their owning event loops when asynchronous work completes. */
class Platform final : public v8::Platform {
    v8::Platform *base_;
public:
    explicit Platform(v8::Platform *base) : base_(base) {}
    v8::PageAllocator *GetPageAllocator() override { return base_->GetPageAllocator(); }
    v8::ThreadIsolatedAllocator *GetThreadIsolatedAllocator() override { return base_->GetThreadIsolatedAllocator(); }
    size_t GetZeroSegmentSize() override { return base_->GetZeroSegmentSize(); }
    void OnCriticalMemoryPressure() override { base_->OnCriticalMemoryPressure(); }
    int NumberOfWorkerThreads() override { return base_->NumberOfWorkerThreads(); }
    std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(v8::Isolate *iso, v8::TaskPriority priority) override {
        {
            std::lock_guard<std::mutex> lock(registry_mutex);
            auto it = runners.find(iso);
            if (it != runners.end()) return it->second;
        }
        return base_->GetForegroundTaskRunner(iso, priority);
    }
    bool IdleTasksEnabled(v8::Isolate *iso) override {
        std::lock_guard<std::mutex> lock(registry_mutex);
        return runners.count(iso) ? false : base_->IdleTasksEnabled(iso);
    }
    std::unique_ptr<v8::ScopedBoostablePriority> CreateBoostablePriorityScope() override { return base_->CreateBoostablePriorityScope(); }
    std::unique_ptr<v8::ScopedBlockingCall> CreateBlockingScope(v8::BlockingType type) override { return base_->CreateBlockingScope(type); }
    double MonotonicallyIncreasingTime() override { return base_->MonotonicallyIncreasingTime(); }
    int64_t CurrentClockTimeMilliseconds() override { return base_->CurrentClockTimeMilliseconds(); }
    double CurrentClockTimeMillis() override { return base_->CurrentClockTimeMillis(); }
    double CurrentClockTimeMillisecondsHighResolution() override { return base_->CurrentClockTimeMillisecondsHighResolution(); }
    StackTracePrinter GetStackTracePrinter() override { return base_->GetStackTracePrinter(); }
    v8::TracingController *GetTracingController() override { return base_->GetTracingController(); }
    void DumpWithoutCrashing() override { base_->DumpWithoutCrashing(); }
    v8::HighAllocationThroughputObserver *GetHighAllocationThroughputObserver() override { return base_->GetHighAllocationThroughputObserver(); }
protected:
    std::unique_ptr<v8::JobHandle> CreateJobImpl(v8::TaskPriority priority, std::unique_ptr<v8::JobTask> task, const v8::SourceLocation &location) override { return base_->CreateJob(priority, std::move(task), location); }
    void PostTaskOnWorkerThreadImpl(v8::TaskPriority priority, std::unique_ptr<v8::Task> task, const v8::SourceLocation &location) override { base_->PostTaskOnWorkerThread(priority, std::move(task), location); }
    void PostDelayedTaskOnWorkerThreadImpl(v8::TaskPriority priority, std::unique_ptr<v8::Task> task, double delay, const v8::SourceLocation &location) override { base_->PostDelayedTaskOnWorkerThread(priority, std::move(task), delay, location); }
};
}

std::unique_ptr<v8::Platform> js_task_platform(v8::Platform *base) { return std::make_unique<Platform>(base); }
std::shared_ptr<JsForegroundTasks> js_register_foreground_tasks(v8::Isolate *iso, std::function<void()> wake) {
    auto tasks = std::make_shared<JsForegroundTasks>(std::move(wake));
    std::lock_guard<std::mutex> lock(registry_mutex);
    runners.emplace(iso, tasks);
    return tasks;
}
void js_unregister_foreground_tasks(v8::Isolate *iso) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    runners.erase(iso);
}
