#pragma once

#include <atomic>
#include <condition_variable>
#include <core/noncopyable.hpp>
#include <datastructures/lockfree_queue.hpp>
#include <functional>
#include <future>
#include <logger.hpp>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
namespace Astra::concurrent {

    using namespace datastructures;
    struct Task {
        std::function<void()> func;
        int priority;

        bool operator<(const Task &other) const {
            return priority > other.priority;// 小值优先
        }
    };

    class ThreadPool : private NonCopyable {
    public:
        explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency())
            : stop_flag_(false), paused_(false) {
            if (num_threads == 0) num_threads = 1;

            workers_.reserve(num_threads);
            for (size_t i = 0; i < num_threads; ++i) {
                workers_.emplace_back(std::make_unique<Worker>());
                threads_.emplace_back(&ThreadPool::WorkerLoop, this, i);
            }
        }

        ~ThreadPool() {
            Stop();
            JoinAll();

            // 清空全局队列
            std::function<void()> task;
            while (tasks_.Pop(task)) {
                task();
            }
        }

        template<typename F, typename... Args>
        [[nodiscard]] std::future<std::invoke_result_t<F, Args...>> Submit(F &&f, Args &&...args) {
            return SubmitWithPriority(0, std::forward<F>(f), std::forward<Args>(args)...);
        }

        template<typename F, typename... Args>
        [[nodiscard]] std::future<std::invoke_result_t<F, Args...>>
        SubmitWithPriority(int priority, F &&f, Args &&...args) {
            using return_type = std::invoke_result_t<F, Args...>;

            auto task = std::make_shared<std::packaged_task<return_type()>>(
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...));

            std::future<return_type> result = task->get_future();

            // 改进的任务分发策略
            static thread_local size_t last_worker = 0;
            size_t target_worker = (last_worker + 1) % workers_.size();
            last_worker = target_worker;

            {
                std::lock_guard<std::mutex> lock(workers_[target_worker]->tasks_mutex_);
                workers_[target_worker]->local_tasks_.push(
                        Task{[task]() { (*task)(); }, priority});
                workers_[target_worker]->has_tasks.store(true);
            }
            workers_[target_worker]->cv.notify_one();

            return result;
        }

        template<typename F, typename Callback>
        void SubmitWithCallback(F &&f, Callback &&cb) {
            if (paused_.load(std::memory_order_acquire)) {
                // 如果处于暂停状态，直接执行回调（或者可以选择排队）
                cb();
                return;
            }

            auto task = std::make_shared<std::function<void()>>(std::forward<F>(f));
            auto callback = std::forward<Callback>(cb);

            if (!task || !*task) {
                ZEN_LOG_WARN("Submitted empty task to thread pool");
                return;
            }

            tasks_.Push([task, callback]() {
                if (task && *task) {
                    (*task)();
                }
                callback();
            });

            condition_.notify_one();
        }

        void Pause() {
            paused_.store(true, std::memory_order_release);
        }

        void Resume() {
            paused_.store(false, std::memory_order_release);
            condition_.notify_all();
        }

        void Stop() {
            stop_flag_.store(true, std::memory_order_release);

            // 唤醒所有Worker
            for (auto &worker: workers_) {
                worker->cv.notify_all();
            }

            // 清空全局队列
            std::function<void()> task;
            while (tasks_.Pop(task)) {
                task();// 执行剩余任务
            }
        }

    private:
        struct Worker {
            std::priority_queue<Task> local_tasks_;
            std::mutex tasks_mutex_;
            std::condition_variable cv;        // 每个Worker独立的唤醒机制
            std::atomic<bool> has_tasks{false};// 无锁状态检查
        };

        void WorkerLoop(size_t worker_id) {
            auto &current_worker = *workers_[worker_id];
            ZEN_LOG_DEBUG("Worker {} started", worker_id);

            while (!stop_flag_.load(std::memory_order_acquire)) {
                // ===== 1. 检查暂停状态 =====
                if (paused_.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                // ===== 2. 处理本地任务队列 =====
                Task local_task;
                bool has_local_task = false;

                {// 锁作用域开始
                    std::unique_lock<std::mutex> lock(current_worker.tasks_mutex_);

                    // 等待条件（带超时避免死锁）
                    if (current_worker.cv.wait_for(lock, std::chrono::milliseconds(100),
                                                   [&] {
                                                       return stop_flag_.load() ||
                                                              !current_worker.local_tasks_.empty();
                                                   })) {
                        if (!current_worker.local_tasks_.empty()) {
                            local_task = std::move(current_worker.local_tasks_.top());
                            current_worker.local_tasks_.pop();
                            has_local_task = true;
                            current_worker.has_tasks.store(
                                    !current_worker.local_tasks_.empty(),
                                    std::memory_order_release);
                        }
                    }
                }// 锁作用域结束

                // 执行本地任务前再次检查暂停状态
                if (has_local_task && local_task.func && !paused_.load(std::memory_order_acquire)) {
                    try {
                        local_task.func();
                    } catch (const std::exception &e) {
                        ZEN_LOG_ERROR("Worker {} task failed: {}", worker_id, e.what());
                    }
                    continue;// 优先处理本地任务
                }

                // ===== 3. 任务窃取机制 =====
                if (!stop_flag_.load(std::memory_order_acquire) && !paused_.load(std::memory_order_acquire)) {
                    const size_t steal_attempts = 3;// 最大尝试次数
                    for (size_t attempt = 0; attempt < steal_attempts; ++attempt) {
                        const size_t target_id = (worker_id + attempt) % workers_.size();
                        if (target_id == worker_id) continue;

                        auto &target_worker = *workers_[target_id];

                        // 快速检查（无锁）
                        if (!target_worker.has_tasks.load(std::memory_order_acquire)) {
                            continue;
                        }

                        // 尝试加锁（非阻塞）
                        std::unique_lock<std::mutex> lock(
                                target_worker.tasks_mutex_,
                                std::try_to_lock);

                        if (lock && !target_worker.local_tasks_.empty()) {
                            Task stolen_task = std::move(target_worker.local_tasks_.top());
                            target_worker.local_tasks_.pop();
                            target_worker.has_tasks.store(
                                    !target_worker.local_tasks_.empty(),
                                    std::memory_order_release);
                            lock.unlock();

                            if (stolen_task.func && !paused_.load(std::memory_order_acquire)) {
                                try {
                                    stolen_task.func();
                                } catch (const std::exception &e) {
                                    ZEN_LOG_ERROR("Worker {} stolen task failed: {}",
                                                  worker_id, e.what());
                                }
                                break;// 成功窃取后退出尝试
                            }
                        }
                    }
                }

                // ===== 4. 处理全局队列 =====
                std::function<void()> global_task;
                if (tasks_.Pop(global_task) && !paused_.load(std::memory_order_acquire)) {
                    try {
                        global_task();
                    } catch (const std::exception &e) {
                        ZEN_LOG_ERROR("Worker {} global task failed: {}",
                                      worker_id, e.what());
                    }
                    continue;
                }

                // ===== 5. 后备等待策略 =====
                std::this_thread::yield();
            }

            // ===== 清理阶段 =====
            ZEN_LOG_DEBUG("Worker {} shutting down", worker_id);

            // 清空本地任务
            while (true) {
                Task task;
                {
                    std::lock_guard<std::mutex> lock(current_worker.tasks_mutex_);
                    if (current_worker.local_tasks_.empty()) break;
                    task = std::move(current_worker.local_tasks_.top());
                    current_worker.local_tasks_.pop();
                }
                if (task.func) {
                    try {
                        task.func();
                    } catch (...) {
                        // 确保异常不会传播出析构函数
                    }
                }
            }

            // 清空全局任务（可选）
            std::function<void()> global_task;
            while (tasks_.Pop(global_task)) {
                try {
                    global_task();
                } catch (...) {
                    // 确保异常不会传播出析构函数
                }
            }

            ZEN_LOG_DEBUG("Worker {} exited", worker_id);
        }

        void JoinAll() {
            for (auto &t: threads_) {
                if (t.joinable()) {
                    t.join();
                }
            }
        }

        std::vector<std::unique_ptr<Worker>> workers_;
        std::vector<std::thread> threads_;
        LockFreeQueue<std::function<void()>> tasks_;
        std::atomic<bool> stop_flag_;
        std::atomic<bool> paused_;
        std::condition_variable condition_;
    };

}// namespace Astra::concurrent