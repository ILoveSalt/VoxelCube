#include <VoxelCube/Core/Application.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <VoxelCube/Core/Config.hpp>

namespace
{
    [[noreturn]] void ThrowApplicationConfigError(const vc::Path& sourcePath, std::string_view message)
    {
        std::ostringstream builder;
        builder << "Invalid application config";
        if (!sourcePath.empty())
        {
            builder << " in " << sourcePath.string();
        }

        builder << ": " << message;
        throw std::runtime_error(builder.str());
    }

    const vc::ConfigValue& ResolveApplicationRoot(const vc::ConfigDocument& document)
    {
        const vc::ConfigValue& root = document.GetRoot();
        if (!root.IsObject())
        {
            ThrowApplicationConfigError(document.GetSourcePath(), "root value must be a JSON object.");
        }

        if (const vc::ConfigValue* application = root.Find("application"))
        {
            if (!application->IsObject())
            {
                ThrowApplicationConfigError(document.GetSourcePath(), "`application` must be a JSON object.");
            }

            return *application;
        }

        return root;
    }

    [[nodiscard]] std::string ReadString(const vc::ConfigValue& value, const vc::Path& sourcePath, std::string_view path)
    {
        const std::string* stringValue = value.TryAsString();
        if (stringValue == nullptr)
        {
            std::ostringstream builder;
            builder << '`' << path << "` must be a string.";
            ThrowApplicationConfigError(sourcePath, builder.str());
        }

        return *stringValue;
    }

    [[nodiscard]] bool ReadBoolean(const vc::ConfigValue& value, const vc::Path& sourcePath, std::string_view path)
    {
        const bool* booleanValue = value.TryAsBoolean();
        if (booleanValue == nullptr)
        {
            std::ostringstream builder;
            builder << '`' << path << "` must be a boolean.";
            ThrowApplicationConfigError(sourcePath, builder.str());
        }

        return *booleanValue;
    }

    [[nodiscard]] double ReadDouble(const vc::ConfigValue& value, const vc::Path& sourcePath, std::string_view path)
    {
        const double* numberValue = value.TryAsNumber();
        if (numberValue == nullptr)
        {
            std::ostringstream builder;
            builder << '`' << path << "` must be a number.";
            ThrowApplicationConfigError(sourcePath, builder.str());
        }

        return *numberValue;
    }

    template <typename T>
    [[nodiscard]] T ReadUnsignedInteger(const vc::ConfigValue& value, const vc::Path& sourcePath, std::string_view path)
    {
        const double numericValue = ReadDouble(value, sourcePath, path);
        if (!std::isfinite(numericValue) || numericValue < 0.0 || std::trunc(numericValue) != numericValue)
        {
            std::ostringstream builder;
            builder << '`' << path << "` must be a non-negative integer.";
            ThrowApplicationConfigError(sourcePath, builder.str());
        }

        if (numericValue > static_cast<double>(std::numeric_limits<T>::max()))
        {
            std::ostringstream builder;
            builder << '`' << path << "` is out of range.";
            ThrowApplicationConfigError(sourcePath, builder.str());
        }

        return static_cast<T>(numericValue);
    }
}

namespace vc
{
    ApplicationSpecification LoadApplicationSpecification(const ConfigDocument& document, ApplicationSpecification defaults)
    {
        const ConfigValue& applicationRoot = ResolveApplicationRoot(document);
        const Path& sourcePath = document.GetSourcePath();

        if (const ConfigValue* value = applicationRoot.FindPath("name"))
        {
            defaults.name = ReadString(*value, sourcePath, "application.name");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("window.width"))
        {
            defaults.windowWidth = ReadUnsignedInteger<std::uint32_t>(*value, sourcePath, "application.window.width");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("window.height"))
        {
            defaults.windowHeight = ReadUnsignedInteger<std::uint32_t>(*value, sourcePath, "application.window.height");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("window.resizable"))
        {
            defaults.windowResizable = ReadBoolean(*value, sourcePath, "application.window.resizable");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("window.startMaximized"))
        {
            defaults.startMaximized = ReadBoolean(*value, sourcePath, "application.window.startMaximized");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("window.startFullscreen"))
        {
            defaults.startFullscreen = ReadBoolean(*value, sourcePath, "application.window.startFullscreen");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("logging.minimumLevel"))
        {
            defaults.minimumLogLevel = ParseLogLevel(ReadString(*value, sourcePath, "application.logging.minimumLevel"));
        }

        if (const ConfigValue* value = applicationRoot.FindPath("logging.enableConsole"))
        {
            defaults.logToConsole = ReadBoolean(*value, sourcePath, "application.logging.enableConsole");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("logging.enableFile"))
        {
            defaults.logToFile = ReadBoolean(*value, sourcePath, "application.logging.enableFile");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("logging.filePath"))
        {
            defaults.logFilePath = Path(ReadString(*value, sourcePath, "application.logging.filePath"));
        }

        if (const ConfigValue* value = applicationRoot.FindPath("timing.fixedTimeStep"))
        {
            defaults.fixedTimeStep = ReadDouble(*value, sourcePath, "application.timing.fixedTimeStep");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("timing.targetFrameRate"))
        {
            defaults.targetFrameRate = ReadUnsignedInteger<std::uint32_t>(*value, sourcePath, "application.timing.targetFrameRate");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("timing.maxDeltaTime"))
        {
            defaults.maxDeltaTime = ReadDouble(*value, sourcePath, "application.timing.maxDeltaTime");
        }

        if (const ConfigValue* value = applicationRoot.FindPath("timing.maxFrames"))
        {
            defaults.maxFrames = ReadUnsignedInteger<std::uint64_t>(*value, sourcePath, "application.timing.maxFrames");
        }

        if (defaults.windowWidth == 0 || defaults.windowHeight == 0)
        {
            ThrowApplicationConfigError(sourcePath, "window dimensions must be greater than zero.");
        }

        if (defaults.fixedTimeStep <= 0.0)
        {
            ThrowApplicationConfigError(sourcePath, "fixedTimeStep must be greater than zero.");
        }

        if (defaults.maxDeltaTime <= 0.0)
        {
            ThrowApplicationConfigError(sourcePath, "maxDeltaTime must be greater than zero.");
        }

        return defaults;
    }

    ApplicationSpecification LoadApplicationSpecificationFromFile(const Path& path, ApplicationSpecification defaults)
    {
        return LoadApplicationSpecification(ConfigDocument::LoadFromFile(path), defaults);
    }
}
