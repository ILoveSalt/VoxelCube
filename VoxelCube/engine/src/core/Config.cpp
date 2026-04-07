#include <VoxelCube/Core/Config.hpp>
#include <VoxelCube/Core/FileSystem.hpp>

#include <cctype>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    [[noreturn]] void ThrowConfigError(const vc::Path& sourcePath, std::size_t line, std::size_t column, std::string_view message)
    {
        std::ostringstream builder;
        builder << "Config parse error";
        if (!sourcePath.empty())
        {
            builder << " in " << sourcePath.string();
        }

        builder << " at line " << line << ", column " << column << ": " << message;
        throw std::runtime_error(builder.str());
    }

    void AppendCodePointUtf8(std::string& target, std::uint32_t codePoint)
    {
        if (codePoint <= 0x7F)
        {
            target.push_back(static_cast<char>(codePoint));
            return;
        }

        if (codePoint <= 0x7FF)
        {
            target.push_back(static_cast<char>(0xC0 | (codePoint >> 6U)));
            target.push_back(static_cast<char>(0x80 | (codePoint & 0x3FU)));
            return;
        }

        if (codePoint <= 0xFFFF)
        {
            target.push_back(static_cast<char>(0xE0 | (codePoint >> 12U)));
            target.push_back(static_cast<char>(0x80 | ((codePoint >> 6U) & 0x3FU)));
            target.push_back(static_cast<char>(0x80 | (codePoint & 0x3FU)));
            return;
        }

        target.push_back(static_cast<char>(0xF0 | (codePoint >> 18U)));
        target.push_back(static_cast<char>(0x80 | ((codePoint >> 12U) & 0x3FU)));
        target.push_back(static_cast<char>(0x80 | ((codePoint >> 6U) & 0x3FU)));
        target.push_back(static_cast<char>(0x80 | (codePoint & 0x3FU)));
    }

    class JsonConfigParser
    {
    public:
        JsonConfigParser(std::string_view text, vc::Path sourcePath)
            : m_text(text)
            , m_sourcePath(std::move(sourcePath))
        {
        }

        [[nodiscard]] vc::ConfigDocument ParseDocument()
        {
            SkipWhitespace();
            vc::ConfigValue root = ParseValue();
            SkipWhitespace();

            if (!IsAtEnd())
            {
                Fail("Unexpected trailing characters.");
            }

            return vc::ConfigDocument(std::move(root), std::move(m_sourcePath));
        }

    private:
        [[nodiscard]] vc::ConfigValue ParseValue()
        {
            if (IsAtEnd())
            {
                Fail("Unexpected end of file.");
            }

            switch (const char value = Peek())
            {
            case '{':
                return ParseObject();
            case '[':
                return ParseArray();
            case '"':
                return vc::ConfigValue(ParseString());
            case 't':
                return ParseLiteral("true", vc::ConfigValue(true));
            case 'f':
                return ParseLiteral("false", vc::ConfigValue(false));
            case 'n':
                return ParseLiteral("null", vc::ConfigValue(nullptr));
            default:
                if (value == '-' || IsDigit(value))
                {
                    return vc::ConfigValue(ParseNumber());
                }

                Fail("Unexpected character while parsing value.");
            }
        }

        [[nodiscard]] vc::ConfigValue ParseObject()
        {
            Consume('{', "Expected '{' to begin an object.");
            vc::ConfigValue::Object object;

            SkipWhitespace();
            if (Match('}'))
            {
                return vc::ConfigValue(std::move(object));
            }

            while (true)
            {
                SkipWhitespace();
                if (Peek() != '"')
                {
                    Fail("Expected a quoted key inside an object.");
                }

                const std::string key = ParseString();
                SkipWhitespace();
                Consume(':', "Expected ':' after an object key.");
                SkipWhitespace();

                auto [iterator, inserted] = object.try_emplace(key, ParseValue());
                if (!inserted)
                {
                    (void)iterator;
                    Fail("Duplicate keys are not allowed in .vcconfig.");
                }

                SkipWhitespace();
                if (Match('}'))
                {
                    break;
                }

                Consume(',', "Expected ',' or '}' after an object member.");
            }

            return vc::ConfigValue(std::move(object));
        }

        [[nodiscard]] vc::ConfigValue ParseArray()
        {
            Consume('[', "Expected '[' to begin an array.");
            vc::ConfigValue::Array array;

            SkipWhitespace();
            if (Match(']'))
            {
                return vc::ConfigValue(std::move(array));
            }

            while (true)
            {
                SkipWhitespace();
                array.push_back(ParseValue());
                SkipWhitespace();

                if (Match(']'))
                {
                    break;
                }

                Consume(',', "Expected ',' or ']' after an array element.");
            }

            return vc::ConfigValue(std::move(array));
        }

        [[nodiscard]] std::string ParseString()
        {
            Consume('"', "Expected '\"' to begin a string.");
            std::string result;

            while (!IsAtEnd())
            {
                const char value = Advance();
                if (value == '"')
                {
                    return result;
                }

                if (value == '\\')
                {
                    if (IsAtEnd())
                    {
                        Fail("Unexpected end of file inside an escape sequence.");
                    }

                    switch (const char escape = Advance())
                    {
                    case '"':
                    case '\\':
                    case '/':
                        result.push_back(escape);
                        break;
                    case 'b':
                        result.push_back('\b');
                        break;
                    case 'f':
                        result.push_back('\f');
                        break;
                    case 'n':
                        result.push_back('\n');
                        break;
                    case 'r':
                        result.push_back('\r');
                        break;
                    case 't':
                        result.push_back('\t');
                        break;
                    case 'u':
                        ParseUnicodeEscape(result);
                        break;
                    default:
                        Fail("Unsupported escape sequence in string.");
                    }

                    continue;
                }

                if (static_cast<unsigned char>(value) < 0x20)
                {
                    Fail("Control characters must be escaped inside strings.");
                }

                result.push_back(value);
            }

            Fail("Unterminated string literal.");
        }

        [[nodiscard]] double ParseNumber()
        {
            const std::size_t startIndex = m_index;

            Match('-');

            if (!Match('0'))
            {
                if (!IsDigit(Peek()))
                {
                    Fail("Expected a digit while parsing a number.");
                }

                while (IsDigit(Peek()))
                {
                    Advance();
                }
            }

            if (Match('.'))
            {
                if (!IsDigit(Peek()))
                {
                    Fail("Expected at least one digit after the decimal point.");
                }

                while (IsDigit(Peek()))
                {
                    Advance();
                }
            }

            if (Peek() == 'e' || Peek() == 'E')
            {
                Advance();
                if (Peek() == '+' || Peek() == '-')
                {
                    Advance();
                }

                if (!IsDigit(Peek()))
                {
                    Fail("Expected a digit in the exponent.");
                }

                while (IsDigit(Peek()))
                {
                    Advance();
                }
            }

            const std::string token(m_text.substr(startIndex, m_index - startIndex));

            try
            {
                const double value = std::stod(token);
                if (!std::isfinite(value))
                {
                    Fail("Only finite numbers are supported.");
                }

                return value;
            }
            catch (const std::exception&)
            {
                Fail("Invalid numeric literal.");
            }
        }

        [[nodiscard]] vc::ConfigValue ParseLiteral(std::string_view literal, vc::ConfigValue value)
        {
            for (const char expected : literal)
            {
                if (Advance() != expected)
                {
                    Fail("Invalid JSON literal.");
                }
            }

            return value;
        }

        void ParseUnicodeEscape(std::string& output)
        {
            std::uint32_t codePoint = ParseHexCodeUnit();
            if (codePoint >= 0xD800U && codePoint <= 0xDBFFU)
            {
                Consume('\\', "Expected low surrogate pair.");
                Consume('u', "Expected low surrogate pair.");
                const std::uint32_t lowSurrogate = ParseHexCodeUnit();
                if (lowSurrogate < 0xDC00U || lowSurrogate > 0xDFFFU)
                {
                    Fail("Invalid low surrogate in Unicode escape.");
                }

                codePoint = 0x10000U + (((codePoint - 0xD800U) << 10U) | (lowSurrogate - 0xDC00U));
            }
            else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU)
            {
                Fail("Unexpected low surrogate without a preceding high surrogate.");
            }

            AppendCodePointUtf8(output, codePoint);
        }

        [[nodiscard]] std::uint32_t ParseHexCodeUnit()
        {
            std::uint32_t codePoint = 0;
            for (int index = 0; index < 4; ++index)
            {
                if (IsAtEnd())
                {
                    Fail("Unexpected end of file inside a Unicode escape.");
                }

                const char value = Advance();
                codePoint <<= 4U;
                if (value >= '0' && value <= '9')
                {
                    codePoint |= static_cast<std::uint32_t>(value - '0');
                }
                else if (value >= 'a' && value <= 'f')
                {
                    codePoint |= static_cast<std::uint32_t>(10 + (value - 'a'));
                }
                else if (value >= 'A' && value <= 'F')
                {
                    codePoint |= static_cast<std::uint32_t>(10 + (value - 'A'));
                }
                else
                {
                    Fail("Invalid hexadecimal digit in Unicode escape.");
                }
            }

            return codePoint;
        }

        void SkipWhitespace()
        {
            while (!IsAtEnd())
            {
                const unsigned char value = static_cast<unsigned char>(Peek());
                if (!std::isspace(value))
                {
                    break;
                }

                Advance();
            }
        }

        bool Match(char expected)
        {
            if (Peek() != expected)
            {
                return false;
            }

            Advance();
            return true;
        }

        void Consume(char expected, std::string_view message)
        {
            if (Peek() != expected)
            {
                Fail(message);
            }

            Advance();
        }

        [[nodiscard]] bool IsAtEnd() const noexcept
        {
            return m_index >= m_text.size();
        }

        [[nodiscard]] char Peek() const noexcept
        {
            return IsAtEnd() ? '\0' : m_text[m_index];
        }

        char Advance()
        {
            if (IsAtEnd())
            {
                Fail("Unexpected end of file.");
            }

            const char value = m_text[m_index++];
            if (value == '\n')
            {
                ++m_line;
                m_column = 1;
            }
            else
            {
                ++m_column;
            }

            return value;
        }

        [[nodiscard]] static bool IsDigit(char value) noexcept
        {
            return value >= '0' && value <= '9';
        }

        [[noreturn]] void Fail(std::string_view message) const
        {
            ThrowConfigError(m_sourcePath, m_line, m_column, message);
        }

    private:
        std::string_view m_text;
        vc::Path m_sourcePath;
        std::size_t m_index = 0;
        std::size_t m_line = 1;
        std::size_t m_column = 1;
    };

    std::string_view StripUtf8Bom(std::string_view text) noexcept
    {
        constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";
        if (text.substr(0, kUtf8Bom.size()) == kUtf8Bom)
        {
            return text.substr(kUtf8Bom.size());
        }

        return text;
    }
}

namespace vc
{
    ConfigValue::ConfigValue(std::nullptr_t)
        : m_type(Type::Null)
    {
    }

    ConfigValue::ConfigValue(bool booleanValue)
        : m_type(Type::Boolean)
        , m_booleanValue(booleanValue)
    {
    }

    ConfigValue::ConfigValue(double numberValue)
        : m_type(Type::Number)
        , m_numberValue(numberValue)
    {
    }

    ConfigValue::ConfigValue(std::string stringValue)
        : m_type(Type::String)
        , m_stringValue(std::move(stringValue))
    {
    }

    ConfigValue::ConfigValue(const char* stringValue)
        : ConfigValue(std::string(stringValue != nullptr ? stringValue : ""))
    {
    }

    ConfigValue::ConfigValue(Array arrayValue)
        : m_type(Type::Array)
        , m_arrayValue(std::move(arrayValue))
    {
    }

    ConfigValue::ConfigValue(Object objectValue)
        : m_type(Type::Object)
        , m_objectValue(std::move(objectValue))
    {
    }

    ConfigValue::Type ConfigValue::GetType() const noexcept
    {
        return m_type;
    }

    bool ConfigValue::IsNull() const noexcept
    {
        return m_type == Type::Null;
    }

    bool ConfigValue::IsBoolean() const noexcept
    {
        return m_type == Type::Boolean;
    }

    bool ConfigValue::IsNumber() const noexcept
    {
        return m_type == Type::Number;
    }

    bool ConfigValue::IsString() const noexcept
    {
        return m_type == Type::String;
    }

    bool ConfigValue::IsArray() const noexcept
    {
        return m_type == Type::Array;
    }

    bool ConfigValue::IsObject() const noexcept
    {
        return m_type == Type::Object;
    }

    const bool* ConfigValue::TryAsBoolean() const noexcept
    {
        return IsBoolean() ? &m_booleanValue : nullptr;
    }

    const double* ConfigValue::TryAsNumber() const noexcept
    {
        return IsNumber() ? &m_numberValue : nullptr;
    }

    const std::string* ConfigValue::TryAsString() const noexcept
    {
        return IsString() ? &m_stringValue : nullptr;
    }

    const ConfigValue::Array* ConfigValue::TryAsArray() const noexcept
    {
        return IsArray() ? &m_arrayValue : nullptr;
    }

    const ConfigValue::Object* ConfigValue::TryAsObject() const noexcept
    {
        return IsObject() ? &m_objectValue : nullptr;
    }

    const ConfigValue* ConfigValue::Find(std::string_view key) const noexcept
    {
        const auto* object = TryAsObject();
        if (object == nullptr)
        {
            return nullptr;
        }

        const auto iterator = object->find(key);
        return iterator != object->end() ? &iterator->second : nullptr;
    }

    const ConfigValue* ConfigValue::FindPath(std::string_view path) const noexcept
    {
        if (path.empty())
        {
            return this;
        }

        const ConfigValue* current = this;
        std::size_t start = 0;

        while (start <= path.size())
        {
            const std::size_t separator = path.find('.', start);
            const std::size_t segmentLength = separator == std::string_view::npos ? path.size() - start : separator - start;
            if (segmentLength == 0)
            {
                return nullptr;
            }

            current = current->Find(path.substr(start, segmentLength));
            if (current == nullptr)
            {
                return nullptr;
            }

            if (separator == std::string_view::npos)
            {
                return current;
            }

            start = separator + 1;
        }

        return current;
    }

    ConfigDocument::ConfigDocument(ConfigValue root, Path sourcePath)
        : m_root(std::move(root))
        , m_sourcePath(std::move(sourcePath))
    {
    }

    ConfigDocument ConfigDocument::Parse(std::string_view text, Path sourcePath)
    {
        return JsonConfigParser(StripUtf8Bom(text), std::move(sourcePath)).ParseDocument();
    }

    ConfigDocument ConfigDocument::LoadFromFile(const Path& path)
    {
        const std::string fileContents = FileSystem::ReadText(path);
        return Parse(fileContents, path);
    }

    const ConfigValue& ConfigDocument::GetRoot() const noexcept
    {
        return m_root;
    }

    const Path& ConfigDocument::GetSourcePath() const noexcept
    {
        return m_sourcePath;
    }
}
