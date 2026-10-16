#pragma once
#include <v8-platform.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>

/* An isolate's foreground tasks belong to its event loop. Posting wakes that
 * loop; delayed tasks contribute a deadline instead of requiring polling.
 * Only the owning thread calls RunOne, at the outermost event-loop level. */
class JsForegroundTasks final : public v8::TaskRunner {
    using Clock = std::chrono::steady_clock;
    std::mutex mutex_;
    std::multimap<Clock::time_point, std::unique_ptr<v8::Task>> tasks_;
    std::function<void()> wake_;
    bool closed_ = false;
    bool running_ = false; // Owning thread only: non-nestable tasks cannot reenter.

    void Post(std::unique_ptr<v8::Task> task, double delay) {
        auto due = Clock::now();
        if (delay > 0) {
            const double maximum = std::chrono::duration<double>(Clock::time_point::max() - due).count();
            due = delay >= maximum ? Clock::time_point::max()
                                   : due + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(delay));
        }
        std::function<void()> wake;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            tasks_.emplace(due, std::move(task));
            wake = wake_;
        }
        // Never call into the event loop while holding the queue lock.
        if (wake) wake();
    }
public:
    explicit JsForegroundTasks(std::function<void()> wake) : wake_(std::move(wake)) {}
    bool IdleTasksEnabled() override { return false; }
    bool NonNestableTasksEnabled() const override { return true; }
    bool NonNestableDelayedTasksEnabled() const override { return true; }
    double NextDelayMs() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tasks_.empty()) return std::numeric_limits<double>::infinity();
        return std::max(0.0, std::chrono::duration<double, std::milli>(tasks_.begin()->first - Clock::now()).count());
    }
    bool RunOne() {
        if (running_) return false;
        std::unique_ptr<v8::Task> task;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (tasks_.empty() || tasks_.begin()->first > Clock::now()) return false;
            task = std::move(tasks_.begin()->second);
            tasks_.erase(tasks_.begin());
        }
        struct Running {
            bool &flag;
            explicit Running(bool &f) : flag(f) { flag = true; }
            ~Running() { flag = false; }
        } running(running_);
        task->Run();
        return true;
    }
    void Close() {
        decltype(tasks_) abandoned;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
            wake_ = {};
            abandoned.swap(tasks_);
        }
        // Task destruction may itself post work; release it outside the lock.
    }
protected:
    void PostTaskImpl(std::unique_ptr<v8::Task> t, const v8::SourceLocation &) override { Post(std::move(t), 0); }
    void PostNonNestableTaskImpl(std::unique_ptr<v8::Task> t, const v8::SourceLocation &) override { Post(std::move(t), 0); }
    void PostDelayedTaskImpl(std::unique_ptr<v8::Task> t, double delay, const v8::SourceLocation &) override { Post(std::move(t), delay); }
    void PostNonNestableDelayedTaskImpl(std::unique_ptr<v8::Task> t, double delay, const v8::SourceLocation &) override { Post(std::move(t), delay); }
};

std::unique_ptr<v8::Platform> js_task_platform(v8::Platform *base);
// Register between Isolate::Allocate and Initialize: V8 caches task runners.
std::shared_ptr<JsForegroundTasks> js_register_foreground_tasks(v8::Isolate *, std::function<void()> wake);
void js_unregister_foreground_tasks(v8::Isolate *);
