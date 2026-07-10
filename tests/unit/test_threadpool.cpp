#include <catch2/catch_test_macros.hpp>

#include "threadpool/thread_pool.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>

namespace{

std::atomic<int> g_counter{0};
void incr(void*){ g_counter.fetch_add(1); }

std::atomic<bool> g_release{false};
void block_until_released(void*){
    while(!g_release.load()){
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::mutex g_ids_mu;
std::set<std::thread::id> g_ids;
void record_thread_id(void*){
    std::lock_guard<std::mutex> lock(g_ids_mu);
    g_ids.insert(std::this_thread::get_id());
}

} // namespace

TEST_CASE("thread pool: every queued task runs exactly once", "[threadpool]"){
    g_counter = 0;
    {
        ThreadPool pool(4);
        for(int i = 0; i < 1000; i++){
            pool.queue(&incr, nullptr);
        }
    }   // destructor drains the queue, then joins
    REQUIRE(g_counter.load() == 1000);
}

TEST_CASE("thread pool: queue() never blocks the producer", "[threadpool]"){
    g_counter = 0;
    g_release = false;
    {
        ThreadPool pool(1);                       // single worker...
        pool.queue(&block_until_released, nullptr);   // ...fully occupied

        auto t0 = std::chrono::steady_clock::now();
        pool.queue(&incr, nullptr);               // must return immediately
        auto dt = std::chrono::steady_clock::now() - t0;
        REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(dt).count() < 100);

        g_release = true;                         // let the worker finish
    }
    REQUIRE(g_counter.load() == 1);               // queued task still ran
}

TEST_CASE("thread pool: work runs off the calling thread", "[threadpool]"){
    {
        std::lock_guard<std::mutex> lock(g_ids_mu);
        g_ids.clear();
    }
    {
        ThreadPool pool(4);
        for(int i = 0; i < 100; i++){
            pool.queue(&record_thread_id, nullptr);
        }
    }
    std::lock_guard<std::mutex> lock(g_ids_mu);
    REQUIRE(!g_ids.empty());
    REQUIRE(g_ids.count(std::this_thread::get_id()) == 0);
}
