#include "core/astra.hpp"
#include "core/types.hpp"
#include <gtest/gtest.h>

TEST(CoreTest, BasicTypesAreDefined) {
    EXPECT_EQ(sizeof(Astra::i8), 1);
    EXPECT_EQ(sizeof(Astra::i16), 2);
    EXPECT_EQ(sizeof(Astra::i32), 4);
    EXPECT_EQ(sizeof(Astra::i64), 8);

    EXPECT_EQ(sizeof(Astra::u8), 1);
    EXPECT_EQ(sizeof(Astra::u16), 2);
    EXPECT_EQ(sizeof(Astra::u32), 4);
    EXPECT_EQ(sizeof(Astra::u64), 8);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}