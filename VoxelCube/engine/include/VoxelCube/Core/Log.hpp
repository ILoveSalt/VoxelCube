#pragma once

#include <ostream>
#include <string>
#include <string_view>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    enum class LogLevel
    {
        Trace = 0,
        Info,
        Warn,
        Error
    };

    struct LogSpecification
    {
        std::string name = "VoxelCube";
        LogLevel minimumLevel = LogLevel::Trace;
        bool enableConsole = true;
        bool enableFile = true;
        Path filePath;
    };

    class Log
    {
    public:
        static void Initialize(LogSpecification specification = {});
        static void Shutdown();

        [[nodiscard]] static bool IsInitialized();
        static void SetMinimumLevel(LogLevel level);
        [[nodiscard]] static LogLevel GetMinimumLevel();
        static void SetOutput(std::ostream& stream);
        static void Flush();
        [[nodiscard]] static const Path& GetFilePath();

        static void Write(LogLevel level, std::string_view message, const char* file, int line);
    };

    [[nodiscard]] LogLevel ParseLogLevel(std::string_view name);
    [[nodiscard]] std::string_view ToString(LogLevel level);
}

#define VC_LOG_TRACE(message) ::vc::Log::Write(::vc::LogLevel::Trace, (message), __FILE__, __LINE__)
#define VC_LOG_INFO(message) ::vc::Log::Write(::vc::LogLevel::Info, (message), __FILE__, __LINE__)
#define VC_LOG_WARN(message) ::vc::Log::Write(::vc::LogLevel::Warn, (message), __FILE__, __LINE__)
#define VC_LOG_ERROR(message) ::vc::Log::Write(::vc::LogLevel::Error, (message), __FILE__, __LINE__)
