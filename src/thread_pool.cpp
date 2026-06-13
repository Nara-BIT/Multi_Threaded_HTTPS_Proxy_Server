#include "thread_pool.h"

ThreadPool::ThreadPool(size_t num_threads) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back([this] {
            // Each worker loops forever: wait → pick task → execute
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    // Block until there's a task or we're told to stop
                    cv.wait(lock, [this] {
                        return stop.load() || !tasks.empty();
                    });
                    if (stop && tasks.empty()) return;  // Clean shutdown
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();  // Execute outside the lock
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        stop = true;
    }
    cv.notify_all();
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        tasks.push(std::move(task));
    }
    cv.notify_one();  // Wake exactly one sleeping worker
}

size_t ThreadPool::queue_size() const {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tasks.size();
}
