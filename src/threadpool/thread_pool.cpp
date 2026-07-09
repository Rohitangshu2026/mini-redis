#include "threadpool/thread_pool.h"

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) num_threads = 1;
    threads_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; i++) {
        threads_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    not_empty_.notify_all();             // wake everyone so they can exit
    for (std::thread& t : threads_) {
        t.join();                        // workers drain the queue first
    }
}

void ThreadPool::queue(void (*f)(void*), void* arg) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back(Work{f, arg});
    }
    not_empty_.notify_one();             // wake one sleeping worker, if any
}

void ThreadPool::worker_loop() {
    while (true) {
        std::unique_lock<std::mutex> lock(mu_);
        // The condition is re-checked in a loop (inside wait): wakeups can
        // be spurious, and a competing worker may consume the item first.
        not_empty_.wait(lock, [this] { return !queue_.empty() || stop_; });
        if (queue_.empty()) {
            return;                      // stop requested and nothing left
        }
        Work w = queue_.front();
        queue_.pop_front();
        lock.unlock();                   // never run tasks holding the lock
        w.f(w.arg);
    }
}
