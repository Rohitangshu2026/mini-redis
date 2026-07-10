#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// Fixed-size worker pool for fire-and-forget tasks — the classic
// producer/consumer arrangement: producers push work under a mutex and
// signal; workers sleep on a condition variable until there is work.
//
// Tasks are a plain function pointer + argument, so queueing a task never
// allocates. The intended producer is the event loop, which must never
// block: queue() only takes the mutex long enough to push one item.
class ThreadPool{
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();                       // drains queued work, then joins

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueue one task; wakes one sleeping worker. Never blocks the caller.
    void queue(void (*f)(void*), void* arg);

private:
    struct Work{
        void (*f)(void*) = nullptr;
        void* arg        = nullptr;
    };

    void worker_loop();

    std::vector<std::thread> threads_;
    std::deque<Work>         queue_;
    std::mutex               mu_;
    std::condition_variable  not_empty_;
    bool                     stop_ = false;
};
