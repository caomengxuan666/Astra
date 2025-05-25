#pragma once

#include <atomic>
#include <condition_variable>
#include <core/noncopyable.hpp>
#include <datastructures/lockfree_queue.hpp>
#include <functional>
#include <future>
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
        [[nodiscard]] std::future<std::invoke_result_t<F, Args...>> SubmitWithPriority(int priority, F &&f, Args &&...args) {
            using return_type = std::invoke_result_t<F, Args...>;

            auto task = std::make_shared<std::packaged_task<return_type()>>(
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...));

            std::future<return_type> result = task->get_future();

            size_t min_index = 0;
            for (size_t i = 1; i < workers_.size(); ++i) {
                if (workers_[i]->local_tasks_.size() < workers_[min_index]->local_tasks_.size()) {
                    min_index = i;
                }
            }

            {
                std::unique_lock<std::mutex> lock(workers_[min_index]->tasks_mutex_);
                workers_[min_index]->local_tasks_.push(Task{[task]() { (*task)(); }, priority});
            }

            condition_.notify_one();
            return result;
        }

        template<typename F, typename Callback>
        void SubmitWithCallback(F &&f, Callback &&cb) {
            auto task = std::make_shared<std::function<void()>>(std::forward<F>(f));
            auto callback = std::forward<Callback>(cb);

            if (!task || !*task) {
                // 防止空任务被推入
                ZEN_LOG_WARN("Submitted empty task to thread pool");
                return;
            }

            tasks_.Push([task, callback]() {
                if (task && *task) {
                    (*task)();
                }
                callback();
            });

            condition_.notify_one();// ✅ 唤醒线程处理任务
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
            condition_.notify_all();
        }

    private:
        struct Worker {
            std::priority_queue<Task> local_tasks_;
            mutable std::mutex tasks_mutex_;
        };

        void WorkerLoop(size_t worker_id) {
            while (!stop_flag_.load(std::memory_order_acquire)) {
                if (paused_.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                Task task;
                bool success = false;

                {
                    std::unique_lock<std::mutex> lock(workers_[worker_id]->tasks_mutex_);
                    if (!workers_[worker_id]->local_tasks_.empty()) {
                        task = workers_[worker_id]->local_tasks_.top();
                        workers_[worker_id]->local_tasks_.pop();
                        success = true;
                    }
                }

                if (!success) {
                    // 尝试从其他线程窃取任务
                    for (size_t i = 0; i < workers_.size(); ++i) {
                        if (i == worker_id) continue;

                        std::unique_lock<std::mutex> lock(workers_[i]->tasks_mutex_);
                        if (!workers_[i]->local_tasks_.empty()) {
                            task = workers_[i]->local_tasks_.top();
                            workers_[i]->local_tasks_.pop();
                            success = true;
                            break;
                        }
                    }
                }

                if (!success) {
                    // ✅ 最后尝试从全局队列消费任务
                    std::function<void()> global_task;
                    if (tasks_.Pop(global_task)) {
                        global_task();
                        success = true;
                    }
                }

                if (success && task.func) {
                    task.func();// ✅ 增加空检查
                } else {
                    std::this_thread::yield();
                }
            }

            // 清空本地任务
            while (true) {
                Task task;
                {
                    std::unique_lock<std::mutex> lock(workers_[worker_id]->tasks_mutex_);
                    if (workers_[worker_id]->local_tasks_.empty()) break;
                    task = workers_[worker_id]->local_tasks_.top();
                    workers_[worker_id]->local_tasks_.pop();
                }
                task.func();
            }

            // 清空全局任务（可选）
            std::function<void()> global_task;
            while (tasks_.Pop(global_task)) {
                global_task();
            }
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