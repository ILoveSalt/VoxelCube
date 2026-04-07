#include <VoxelCube/Core/Log.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <VoxelCube/Core/FileSystem.hpp>

#if defined(VC_HAS_SPDLOG)
#include <memory>
#include <vector>

#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#endif

namespace
{
    [[nodiscard]] std::string MakeSafeFileStem(std::string_view name)
    {
        std::string stem;
        stem.reserve(name.size());

        for (const unsigned char value : name)
        {
            if (std::isalnum(value))
            {
                stem.push_back(static_cast<char>(std::tolower(value)));
            }
            else
            {
                stem.push_back('_');
            }
        }

        if (stem.empty())
        {
            stem = "voxelcube";
        }

        return stem;
    }

    [[nodiscard]] vc::Path BuildDefaultLogPath(std::string_view name)
    {
        return vc::Path("logs") / (MakeSafeFileStem(name) + ".log");
    }

    [[nodiscard]] std::string BuildFormattedMessage(vc::LogLevel level, std::string_view message, const char* file, int line)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);

        std::tm localTime {};
#if defined(_MSC_VER)
        localtime_s(&localTime, &timestamp);
#else
        localtime_r(&timestamp, &localTime);
#endif

        std::ostringstream builder;
        builder << std::put_time(&localTime, "%H:%M:%S")
                << " [" << vc::ToString(level) << "] "
                << message
                << " (" << file << ":" << line << ")";
        return builder.str();
    }

#if defined(VC_HAS_SPDLOG)
    [[nodiscard]] spdlog::level::level_enum ToSpdlogLevel(vc::LogLevel level)
    {
        switch (level)
        {
        case vc::LogLevel::Trace:
            return spdlog::level::trace;
        case vc::LogLevel::Info:
            return spdlog::level::info;
        case vc::LogLevel::Warn:
            return spdlog::level::warn;
        case vc::LogLevel::Error:
            return spdlog::level::err;
        }

        return spdlog::level::info;
    }
#endif

    struct LogState
    {
        std::mutex mutex;
        vc::LogSpecification specification {};
        std::ostream* consoleOutput = &std::cout;
        std::ofstream fileOutput;
        bool initialized = false;

#if defined(VC_HAS_SPDLOG)
        std::shared_ptr<spdlog::logger> logger;
        bool usingSpdlog = false;
#endif
    };

    LogState& GetLogState()
    {
        static LogState state;
        return state;
    }
}

namespace vc
{
    std::string_view ToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        }

        return "UNKNOWN";
    }

    LogLevel ParseLogLevel(std::string_view name)
    {
        std::string normalized(name);
        std::transform(
            normalized.begin(),
            normalized.end(),
            normalized.begin(),
            [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });

        if (normalized == "trace")
        {
            return LogLevel::Trace;
        }

        if (normalized == "info")
        {
            return LogLevel::Info;
        }

        if (normalized == "warn" || normalized == "warning")
        {
            return LogLevel::Warn;
        }

        if (normalized == "error")
        {
            return LogLevel::Error;
        }

        throw std::runtime_error("Unknown log level: " + std::string(name));
    }

    void Log::Initialize(LogSpecification specification)
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);

        if (state.initialized)
        {
            state.fileOutput.flush();
            state.fileOutput.close();
#if defined(VC_HAS_SPDLOG)
            if (state.logger)
            {
                state.logger->flush();
                state.logger.reset();
            }
            state.usingSpdlog = false;
#endif
        }

        if (specification.filePath.empty())
        {
            specification.filePath = BuildDefaultLogPath(specification.name);
        }

        state.consoleOutput = &std::cout;
        state.specification = std::move(specification);

        if (state.specification.enableFile)
        {
            const Path parentPath = state.specification.filePath.parent_path();
            if (!parentPath.empty())
            {
                FileSystem::CreateDirectories(parentPath);
            }
        }

#if defined(VC_HAS_SPDLOG)
        std::vector<spdlog::sink_ptr> sinks;
        if (state.specification.enableConsole)
        {
            sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        }

        if (state.specification.enableFile)
        {
            sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(state.specification.filePath.string(), true));
        }

        if (!sinks.empty())
        {
            state.logger = std::make_shared<spdlog::logger>("VoxelCube", sinks.begin(), sinks.end());
            state.logger->set_level(ToSpdlogLevel(state.specification.minimumLevel));
            state.logger->set_pattern("%H:%M:%S [%^%l%$] %v (%s:%#)");
            state.usingSpdlog = true;
        }
#endif

        if (!state.specification.enableFile)
        {
            state.fileOutput.close();
        }
        else
        {
            state.fileOutput.open(state.specification.filePath, std::ios::out | std::ios::trunc);
            if (!state.fileOutput.is_open())
            {
                throw std::runtime_error("Failed to open log file: " + state.specification.filePath.string());
            }
        }

        state.initialized = true;
    }

    void Log::Shutdown()
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);

#if defined(VC_HAS_SPDLOG)
        if (state.logger)
        {
            state.logger->flush();
            state.logger.reset();
        }
        state.usingSpdlog = false;
#endif

        if (state.fileOutput.is_open())
        {
            state.fileOutput.flush();
            state.fileOutput.close();
        }

        state.specification = {};
        state.consoleOutput = &std::cout;
        state.initialized = false;
    }

    bool Log::IsInitialized()
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);
        return state.initialized;
    }

    void Log::SetMinimumLevel(LogLevel level)
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);
        state.specification.minimumLevel = level;

#if defined(VC_HAS_SPDLOG)
        if (state.logger)
        {
            state.logger->set_level(ToSpdlogLevel(level));
        }
#endif
    }

    LogLevel Log::GetMinimumLevel()
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);
        return state.specification.minimumLevel;
    }

    void Log::SetOutput(std::ostream& stream)
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);
        state.consoleOutput = &stream;
    }

    void Log::Flush()
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);

#if defined(VC_HAS_SPDLOG)
        if (state.logger)
        {
            state.logger->flush();
        }
#endif

        if (state.fileOutput.is_open())
        {
            state.fileOutput.flush();
        }

        if (state.consoleOutput != nullptr)
        {
            state.consoleOutput->flush();
        }
    }

    const Path& Log::GetFilePath()
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);
        return state.specification.filePath;
    }

    void Log::Write(LogLevel level, std::string_view message, const char* file, int line)
    {
        auto& state = GetLogState();
        std::scoped_lock lock(state.mutex);

        if (!state.initialized || level < state.specification.minimumLevel)
        {
            return;
        }

#if defined(VC_HAS_SPDLOG)
        if (state.logger && state.usingSpdlog)
        {
            state.logger->log(spdlog::source_loc { file, line, "" }, ToSpdlogLevel(level), std::string(message));
            return;
        }
#endif

        const std::string formattedMessage = BuildFormattedMessage(level, message, file, line);

        if (state.specification.enableConsole && state.consoleOutput != nullptr)
        {
            (*state.consoleOutput) << formattedMessage << '\n';
            state.consoleOutput->flush();
        }

        if (state.specification.enableFile && state.fileOutput.is_open())
        {
            state.fileOutput << formattedMessage << '\n';
            state.fileOutput.flush();
        }
    }
}
