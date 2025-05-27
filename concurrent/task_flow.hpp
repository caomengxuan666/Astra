#pragma once

#include <concurrent/task_queue.hpp>

namespace Astra::concurrent {

    // ======================
    // 串行任务组（类似 workflow::SeriesWork）
    // ======================
    class SeriesWork : private NonCopyable {
    public:
        static std::shared_ptr<SeriesWork> Create(std::shared_ptr<TaskQueue> queue) {
            return std::shared_ptr<SeriesWork>(new SeriesWork(std::move(queue)));
        }
        explicit SeriesWork(std::shared_ptr<TaskQueue> queue) : queue_(std::move(queue)) {}

        template<typename F>
        SeriesWork &Then(F &&func) {
            tasks_.push_back(std::forward<F>(func));
            return *this;
        }

        void Run() {
            for (auto &task: tasks_) {
                queue_->Post(task);
            }
        }

    private:
        std::vector<std::function<void()>> tasks_;
        std::shared_ptr<TaskQueue> queue_;
    };

    // ======================
    // 并发任务组（类似 workflow::ParallelGroup）
    // ======================
    class ParallelWork : private NonCopyable {
    public:
        static std::shared_ptr<ParallelWork> Create(std::shared_ptr<TaskQueue> queue) {
            return std::shared_ptr<ParallelWork>(new ParallelWork(std::move(queue)));
        }

        explicit ParallelWork(std::shared_ptr<TaskQueue> queue) : queue_(std::move(queue)) {}


        template<typename F>
        ParallelWork &Add(F &&func) {
            tasks_.push_back(std::forward<F>(func));
            return *this;
        }

        void Run() {
            for (auto &task: tasks_) {
                queue_->Post(task);
            }
        }

    private:
        std::vector<std::function<void()>> tasks_;
        std::shared_ptr<TaskQueue> queue_;
    };

    // ======================
    // 有状态的任务链（带共享上下文）
    // ======================
    template<typename Context>
    class TaskPipeline : private NonCopyable {
    public:
        static std::shared_ptr<TaskPipeline<Context>> Create(std::shared_ptr<TaskQueue> queue, Context initial) {
            return std::shared_ptr<TaskPipeline<Context>>(new TaskPipeline(std::move(queue), std::move(initial)));
        }
        explicit TaskPipeline(std::shared_ptr<TaskQueue> queue, Context initial)
            : queue_(std::move(queue)), context_(std::move(initial)) {}

        template<typename F>
        TaskPipeline &Step(F &&func) {
            auto task = [this, func = std::forward<F>(func)]() mutable {
                func(context_);
            };

            auto future = queue_->Submit(task);
            future.wait();
            return *this;
        }

    private:
        std::shared_ptr<TaskQueue> queue_;
        Context context_;
    };
}// namespace Astra::concurrent