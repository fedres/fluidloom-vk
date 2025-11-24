#include "core/Logger.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace core {

std::shared_ptr<spdlog::logger> Logger::s_logger = nullptr;

void Logger::init(spdlog::level::level_enum logLevel) {
    if (s_logger) {
        return; // Already initialized
    }

    // Create logs directory if it doesn't exist
    std::filesystem::create_directories("logs");

    // Create sinks
    std::vector<spdlog::sink_ptr> sinks;

    // Console sink with color output
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(logLevel);

    // File sink (truncate existing log)
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/engine.log", true);
    file_sink->set_level(spdlog::level::trace); // Log everything to file

    sinks.push_back(console_sink);
    sinks.push_back(file_sink);

    // Create logger
    s_logger = std::make_shared<spdlog::logger>("FluidEngine", sinks.begin(), sinks.end());
    s_logger->set_level(logLevel);

    // Set pattern: [HH:MM:SS.ms] [LEVEL] [thread ID] message
    s_logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [thread %t] %v");

    // Register as default logger
    spdlog::register_logger(s_logger);

    LOG_INFO("Logger initialized successfully");
}

void Logger::shutdown() {
    if (s_logger) {
        spdlog::drop_all();
        s_logger = nullptr;
    }
}

std::shared_ptr<spdlog::logger> Logger::get() {
    if (!s_logger) {
        // Auto-initialize if not done explicitly
        init();
    }
    return s_logger;
}

} // namespace core
