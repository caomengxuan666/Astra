#include "core/astra.hpp"
#include <gtest/gtest.h>
#include <utils/scope_guard.hpp>

TEST(ScopeGuardTest, ShouldInvokeOnExit) {
    bool invoked = false;
    {
        Astra::ScopeGuard guard([&] { invoked = true; });
        EXPECT_FALSE(invoked);
    }
    EXPECT_TRUE(invoked);
}

TEST(ScopeGuardTest, ShouldNotInvokeIfDismissed) {
    bool invoked = false;
    {
        Astra::ScopeGuard guard([&] { invoked = true; });
        guard.Dismiss();
    }
    EXPECT_FALSE(invoked);
}

TEST(ScopeGuardTest, MoveSemantics) {
    bool invoked = false;

    {
        Astra::ScopeGuard guard1([&] { invoked = true; });
        EXPECT_FALSE(guard1.is_dismissed());

        Astra::ScopeGuard guard2(std::move(guard1));// guard2 接管责任

        EXPECT_TRUE(guard1.is_dismissed()); // guard1 已移交
        EXPECT_FALSE(guard2.is_dismissed());// guard2 现在负责清理
    }

    EXPECT_TRUE(invoked);// 清理函数被执行
}