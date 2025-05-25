#include "core/astra.hpp"
#include <gtest/gtest.h>
#include <utils/logger.hpp>

TEST(LoggerTest, BasicLogging) {
    Astra::Logger::GetInstance().SetLevel(Astra::LogLevel::TRACE);

    ZEN_LOG_TRACE("This is a TRACE message");
    ZEN_LOG_DEBUG("This is a DEBUG message");
    ZEN_LOG_INFO("This is an INFO message");
    ZEN_LOG_WARN("This is a WARN message");
    ZEN_LOG_ERROR("This is an ERROR message");
    ZEN_LOG_FATAL("This is a FATAL message");
}

TEST(LoggerTest, FilterByLevel) {
    Astra::Logger::GetInstance().SetLevel(Astra::LogLevel::ERROR);

    ZEN_LOG_TRACE("This TRACE should NOT be shown");
    ZEN_LOG_DEBUG("This DEBUG should NOT be shown");
    ZEN_LOG_INFO("This INFO should NOT be shown");
    ZEN_LOG_WARN("This WARN should NOT be shown");
    ZEN_LOG_ERROR("This ERROR should be shown");
    ZEN_LOG_FATAL("This FATAL should be shown");
}