#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

/*
 * ThreadPool
 * ----------
 * Fixed pool of N worker threads sharing a task queue.
 * Producer (main accept loop) pushes tasks; workers pop and execute.
 *
 * Why thread pool over one thread per connection?
 * - Bounded memory: N threads vs unbounded growth under load
 * - No per-request thread create/destroy overhead (~100µs each)
 * - Prevents thread exhaustion (OS limit ~1000-8000 threads)
 * Interview: "What's the optimal N?" → benchmark under your load profile;
 * CPU-bound: N = num_cores; I/O-bound: N = 2-4x num_cores.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Enqueue a task; thread-safe, can be called from any thread
    void enqueue(std::function<void()> task);

    size_t queue_size() const;

private:
    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> tasks;
    mutable std::mutex                queue_mutex;
    std::condition_variable           cv;
    std::atomic<bool>                 stop{false};
};
