#include "../src/js/v8_tasks.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <thread>
#include <vector>
#include <unistd.h>

struct Task final : v8::Task {
    std::function<void()> run, destroy;
    explicit Task(std::function<void()> r, std::function<void()> d = {}) : run(std::move(r)), destroy(std::move(d)) {}
    ~Task() override { if (destroy) destroy(); }
    void Run() override { run(); }
};

int main() {
    alarm(15);
    int wakes = 0;
    JsForegroundTasks tasks([&] { ++wakes; });
    std::vector<int> order;
    tasks.PostDelayedTask(std::make_unique<Task>([&] { order.push_back(3); }), .04);
    tasks.PostTask(std::make_unique<Task>([&] {
        order.push_back(1);
        tasks.PostNonNestableTask(std::make_unique<Task>([&] { order.push_back(2); }));
        assert(!tasks.RunOne()); // A nested event loop must not run this task.
    }));
    assert(tasks.RunOne() && tasks.RunOne() && !tasks.RunOne());
    assert((order == std::vector<int>{1, 2}));
    assert(tasks.NextDelayMs() > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(45));
    assert(tasks.RunOne() && !tasks.RunOne());
    assert((order == std::vector<int>{1, 2, 3}) && wakes == 3);
    assert(std::isinf(tasks.NextDelayMs()));

    // A blocked worker must wake without polling, including when an earlier
    // deadline is posted after it started waiting for a delayed task.
    std::mutex mutex;
    std::condition_variable cv;
    unsigned version = 0;
    JsForegroundTasks waiting([&] {
        { std::lock_guard<std::mutex> lock(mutex); ++version; }
        cv.notify_one();
    });
    bool ran = false;
    waiting.PostNonNestableDelayedTask(std::make_unique<Task>([] {}), 60);
    std::unique_lock<std::mutex> lock(mutex);
    auto observed = version;
    std::thread producer([&] { waiting.PostTask(std::make_unique<Task>([&] { ran = true; })); });
    assert(cv.wait_for(lock, std::chrono::seconds(1), [&] { return version != observed; }));
    lock.unlock(); producer.join();
    assert(waiting.RunOne() && ran && waiting.NextDelayMs() > 50000);
    waiting.Close();

    // Pending task destruction may reenter the queue during shutdown.
    int destroyed = 0;
    tasks.PostTask(std::make_unique<Task>([] { assert(false); }, [&] {
        ++destroyed;
        tasks.PostTask(std::make_unique<Task>([] { assert(false); }, [&] { ++destroyed; }));
    }));
    tasks.Close();
    assert(destroyed == 2 && !tasks.RunOne() && wakes == 4);

    // Background compilation may finish concurrently with isolate shutdown.
    std::atomic<int> freed{0}, executed{0};
    JsForegroundTasks closing([] {});
    std::vector<std::thread> producers;
    for (int i = 0; i < 4; ++i) producers.emplace_back([&] {
        for (int j = 0; j < 1000; ++j)
            closing.PostTask(std::make_unique<Task>([&] { ++executed; }, [&] { ++freed; }));
    });
    closing.Close();
    for (auto &p : producers) p.join();
    assert(freed == 4000 && executed == 0 && !closing.RunOne());
    puts("foreground task queue: ordering, deadlines, wakeup, non-nesting, reentrancy, concurrent shutdown passed");
}
