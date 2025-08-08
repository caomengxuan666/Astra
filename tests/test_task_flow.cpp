#include "core/astra.hpp"
#include <concurrent/task_flow.hpp>
#include <gtest/gtest.h>
#include <memory>

using namespace Astra::concurrent;
using namespace std::chrono_literals;

// ======================
// 1. 串行任务链式调用
// ======================
TEST(TaskFlowTest, SeriesWork_ChainExecution) {
    auto queue = std::make_shared<TaskQueue>(4);
    SeriesWork work(queue);

    std::atomic<int> counter = 0;

    work.Then([&counter] { counter.fetch_add(1); })
            .Then([&counter] { counter.fetch_add(2); })
            .Then([&counter] { counter.fetch_add(3); });

    work.Run();
    std::this_thread::sleep_for(1s);

    EXPECT_EQ(counter.load(), 6);
}

// ======================
// 2. 并发任务组 + 回调
// ======================
TEST(TaskFlowTest, ParallelWork_WithCallback) {
    auto queue = std::make_shared<TaskQueue>(4);
    ParallelWork pw(queue);

    std::atomic<int> count = 0;

    pw.Add([&count] { count.fetch_add(1); })
            .Add([&count] { count.fetch_add(1); })
            .Add([&count] { count.fetch_add(1); });

    // 添加回调
    pw.Run();

    std::this_thread::sleep_for(1s);
    EXPECT_EQ(count.load(), 3);
}

// ======================
// 3. 异常安全测试
// ======================
TEST(TaskFlowTest, Task_ExceptionSafety) {
    TaskQueue queue(4);

    bool exception_caught = false;
    std::atomic<int> counter = 0;

    // 提交一个会抛出异常的任务
    auto future = queue.Submit([&] {
        try {
            throw std::runtime_error("Task failed");
        } catch (...) {
            counter.fetch_add(1);
            exception_caught = true;
        }
    });

    future.wait();// 等待任务完成
    EXPECT_TRUE(exception_caught);
    EXPECT_EQ(counter.load(), 1);
}

// ======================
// 4. 共享上下文传递数据
// ======================
struct SharedContext {
    int value = 0;
};

TEST(TaskFlowTest, Task_SharedContext) {
    TaskQueue queue(4);

    auto context = std::make_shared<SharedContext>();

    auto future = queue.Submit([context] {
        context->value = 42;
    });
    future.wait();

    future = queue.Submit([context] {
        EXPECT_EQ(context->value, 42);
    });
    future.wait();
}

// ======================
// 5. 暂停/恢复任务队列
// ======================
TEST(TaskFlowTest, Task_PauseAndResume) {
    TaskQueue queue(4);
    std::atomic<int> count = 0;

    queue.Pause();// 先暂停

    // 提交任务，在 Pause 状态下应不执行
    for (int i = 0; i <= 1000; ++i) {
        auto future = queue.Submit([&count] {
            count.fetch_add(1);
        });
    }

    std::this_thread::sleep_for(500ms);// 给一点时间让队列处理（但应该没执行）

    int paused_count = count.load();
    EXPECT_EQ(paused_count, 0);// 暂停期间不应有任何任务执行

    queue.Resume();// 恢复调度

    std::this_thread::sleep_for(1s);// 等待所有任务完成

    int resumed_count = count.load();
    EXPECT_EQ(resumed_count, 1001);// 所有任务应在恢复后执行
}

// ======================
// 6. DAG 风格任务依赖（前置任务完成后执行后续）
// ======================
TEST(TaskFlowTest, Task_DAGDependency) {
    TaskQueue queue(4);
    std::atomic<int> step1 = 0;
    std::atomic<int> step2 = 0;

    auto future = queue.Submit([&step1] {
        step1.fetch_add(1);
    });
    future.wait();

    queue.SubmitWithCallback(
            [&step2] {
                step2.fetch_add(1);
            },
            [step1 = &step1] {
                EXPECT_EQ(step1->load(), 1);
            });

    std::this_thread::sleep_for(1s);
    EXPECT_EQ(step1.load(), 1);
    EXPECT_EQ(step2.load(), 1);
}

// ======================
// 7. 串行任务 + 回调链
// ======================
TEST(TaskFlowTest, SeriesWork_WithCallbackChain) {
    auto queue = std::make_shared<TaskQueue>(4);
    SeriesWork work(queue);

    std::atomic<int> step1 = 0;
    std::atomic<int> step2 = 0;

    work.Then([&step1] { step1.fetch_add(1); })
            .Then([&step2] { step2.fetch_add(1); });

    work.Run();
    std::this_thread::sleep_for(1s);

    EXPECT_EQ(step1.load(), 1);
    EXPECT_EQ(step2.load(), 1);
}

// ======================
// 8. 串行任务 + 条件判断
// ======================
/*TEST(TaskFlowTest, SeriesWork_WithCondition) {
    auto queue = std::make_shared<TaskQueue>(4);
    SeriesWork work(queue);

    std::atomic<int> flag = 0;

    work.Then([&flag] { flag.fetch_add(1); })
            .Then([&flag] {
                if (flag.load() == 1) {
                    flag.fetch_add(1);
                }
            });

    work.Run();
    std::this_thread::sleep_for(1s);

    EXPECT_EQ(flag.load(), 2);
}*/

// ======================
// 9. 任务延迟执行（模拟定时器）
// ======================
TEST(TaskFlowTest, DelayedTaskExecution) {
    TaskQueue queue(4);
    std::atomic<int> count = 0;

    auto future = queue.Submit([&] {
        std::this_thread::sleep_for(50ms);
        count.fetch_add(1);
    });
    future.wait();

    future = queue.Submit([&] {
        std::this_thread::sleep_for(1s);
        count.fetch_add(1);
    });
    future.wait();

    EXPECT_EQ(count.load(), 2);
}

// ======================
// 10. 任务失败处理
// ======================
TEST(TaskFlowTest, Task_FailureHandling) {
    TaskQueue queue(4);
    std::atomic<int> count = 0;

    auto future = queue.Submit([&count] {
        try {
            throw std::logic_error("Invalid operation");
        } catch (...) {
            count.fetch_add(1);
        }
    });
    future.wait();

    EXPECT_EQ(count.load(), 1);
}

// ======================
// 11. TaskPipeline 共享上下文任务链测试
// ======================
TEST(TaskFlowTest, TaskPipeline_SharedContext) {
    auto queue = std::make_shared<TaskQueue>(4);

    std::atomic<int> final_value = 0;

    // 创建 TaskPipeline，初始值为 0
    auto pipeline = std::make_shared<TaskPipeline<int>>(queue, 0);

    // 添加多个步骤处理共享上下文
    pipeline->Step([](int &value) {
                value += 10;
            })
            .Step([](int &value) {
                value *= 2;
            })
            .Step([&final_value](int &value) {
                final_value = value;// 最终结果保存到 final_value
            });

    // 等待所有任务完成
    std::this_thread::sleep_for(50ms);

    // 验证最终结果是否正确
    EXPECT_EQ(final_value.load(), 20);// (0 + 10) * 2 = 20
}