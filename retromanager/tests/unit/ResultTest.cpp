#include <gtest/gtest.h>

#include "retromanager/core/Result.hpp"

using namespace rm;

TEST(Result, HoldsValue) {
    Result<int> result = 42;
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(result.valueOr(0), 42);
}

TEST(Result, HoldsError) {
    Result<int> result = makeError(ErrorCode::NotFound, "/roms");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
    EXPECT_EQ(result.valueOr(7), 7);
    EXPECT_EQ(result.error().describe(), "NotFound: /roms");
}

TEST(Result, MovesValueOut) {
    Result<std::string> result = std::string("payload");
    std::string moved = std::move(result).value();
    EXPECT_EQ(moved, "payload");
}

TEST(Status, DefaultIsSuccess) {
    Status status;
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(success().ok());

    Status failure = makeError(ErrorCode::IoError, "");
    EXPECT_FALSE(failure.ok());
    EXPECT_EQ(failure.error().describe(), "IoError");
}
