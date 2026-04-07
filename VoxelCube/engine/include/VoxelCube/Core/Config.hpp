#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    class ConfigValue
    {
    public:
        using Array = std::vector<ConfigValue>;
        using Object = std::map<std::string, ConfigValue, std::less<>>;

        enum class Type : std::uint8_t
        {
            Null = 0,
            Boolean,
            Number,
            String,
            Array,
            Object
        };

        ConfigValue() = default;
        ConfigValue(std::nullptr_t);
        ConfigValue(bool booleanValue);
        ConfigValue(double numberValue);
        ConfigValue(std::string stringValue);
        ConfigValue(const char* stringValue);
        ConfigValue(Array arrayValue);
        ConfigValue(Object objectValue);

        [[nodiscard]] Type GetType() const noexcept;

        [[nodiscard]] bool IsNull() const noexcept;
        [[nodiscard]] bool IsBoolean() const noexcept;
        [[nodiscard]] bool IsNumber() const noexcept;
        [[nodiscard]] bool IsString() const noexcept;
        [[nodiscard]] bool IsArray() const noexcept;
        [[nodiscard]] bool IsObject() const noexcept;

        [[nodiscard]] const bool* TryAsBoolean() const noexcept;
        [[nodiscard]] const double* TryAsNumber() const noexcept;
        [[nodiscard]] const std::string* TryAsString() const noexcept;
        [[nodiscard]] const Array* TryAsArray() const noexcept;
        [[nodiscard]] const Object* TryAsObject() const noexcept;

        [[nodiscard]] const ConfigValue* Find(std::string_view key) const noexcept;
        [[nodiscard]] const ConfigValue* FindPath(std::string_view path) const noexcept;

    private:
        Type m_type = Type::Null;
        bool m_booleanValue = false;
        double m_numberValue = 0.0;
        std::string m_stringValue;
        Array m_arrayValue;
        Object m_objectValue;
    };

    class ConfigDocument
    {
    public:
        ConfigDocument() = default;
        ConfigDocument(ConfigValue root, Path sourcePath = {});

        [[nodiscard]] static ConfigDocument Parse(std::string_view text, Path sourcePath = {});
        [[nodiscard]] static ConfigDocument LoadFromFile(const Path& path);

        [[nodiscard]] const ConfigValue& GetRoot() const noexcept;
        [[nodiscard]] const Path& GetSourcePath() const noexcept;

    private:
        ConfigValue m_root;
        Path m_sourcePath;
    };
}
