#pragma once
#include <concurrent/thread_pool.hpp>

namespace Astra::concurrent {

    class TaskQueue : private NonCopyable {
    public:
        explicit TaskQueue(size_t num_threads = std::thread::hardware_concurrency())
            : pool_(num_threads) {}

        // 工厂方法：创建并返回 shared_ptr
        static std::shared_ptr<TaskQueue> Create(size_t num_threads = std::thread::hardware_concurrency()) {
            return std::shared_ptr<TaskQueue>(new TaskQueue(num_threads));
        }

        template<typename F, typename... Args>
        [[nodiscard]] std::future<std::invoke_result_t<F, Args...>> Submit(F &&f, Args &&...args) {
            using return_type = std::invoke_result_t<F, Args...>;

            auto task = std::make_shared<std::packaged_task<return_type()>>(
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...));

            std::future<return_type> result = task->get_future();

            // 提交到线程池执行
            (void) pool_.Submit([task]() { (*task)(); });

            return result;
        }

        template<typename F, typename Callback>
        void SubmitWithCallback(F &&f, Callback &&cb) {
            auto task = std::make_shared<std::function<void()>>(std::forward<F>(f));
            auto callback = std::forward<Callback>(cb);

            if (!task || !*task) {
                ZEN_LOG_WARN("Submitted empty task to async queue");
                return;
            }

            (void) pool_.Submit([task, callback]() {
                if (task && *task) {
                    (*task)();
                }
                callback();
            });
        }

        template<typename F>
        void Post(F &&f) {
            (void) pool_.Submit(std::forward<F>(f));
        }

        void Pause() {
            pool_.Pause();
        }

        void Resume() {
            pool_.Resume();
        }

        void Stop() {
            pool_.Stop();
        }

    private:
        ThreadPool pool_;
    };

}// namespace Astra::concurrent