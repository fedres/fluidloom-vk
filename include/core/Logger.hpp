#pragma once

#include <memory>
#include <spdlog/spdlog.h>

namespace core {

/**
 * @brief Thread-safe logging system for the Fluid Engine
 *
 * Uses spdlog with both console and file output.
 * Call Logger::init() once at startup.
 */
class Logger {
public:
    /**
     * Initialize the logger with console and file sinks
     * @param logLevel Log level (trace, debug, info, warn, err, critical)
     */
    static void init(spdlog::level::level_enum logLevel = spdlog::level::info);

    /**
     * Shutdown the logger (flush buffers)
     */
    static void shutdown();

    /**
     * Get the global logger instance
     */
    static std::shared_ptr<spdlog::logger> get();

private:
    static std::shared_ptr<spdlog::logger> s_logger;

    Logger() = delete;
    ~Logger() = delete;
};

// Convenience macros for logging
#define LOG_TRACE(...) core::Logger::get()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) core::Logger::get()->debug(__VA_ARGS__)
#define LOG_INFO(...) core::Logger::get()->info(__VA_ARGS__)
#define LOG_WARN(...) core::Logger::get()->warn(__VA_ARGS__)
#define LOG_ERROR(...) core::Logger::get()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) core::Logger::get()->critical(__VA_ARGS__)

/**
 * @brief Assertion-style logging: logs error and throws if condition is false
 * @param condition Boolean condition to check
 * @param message Error message if condition is false
 */
#define LOG_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            core::Logger::get()->error("CHECK FAILED: {}", message); \
            throw std::runtime_error(std::string("CHECK FAILED: ") + message); \
        } \
    } while (0)

} // namespace core
