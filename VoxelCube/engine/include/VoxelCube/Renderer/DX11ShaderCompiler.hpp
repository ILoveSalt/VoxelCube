#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <d3dcommon.h>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    enum class DX11ShaderStage
    {
        Vertex = 0,
        Pixel,
        Geometry,
        Hull,
        Domain,
        Compute
    };

    struct DX11ShaderMacro
    {
        std::string name;
        std::string value;
    };

    struct DX11ShaderCompileSpecification
    {
        std::string sourceName;
        std::string entryPoint = "Main";
        DX11ShaderStage stage = DX11ShaderStage::Vertex;
        std::uint32_t shaderModelMajor = 5;
        std::uint32_t shaderModelMinor = 0;
#if defined(NDEBUG)
        bool enableDebugInfo = false;
        bool skipOptimization = false;
#else
        bool enableDebugInfo = true;
        bool skipOptimization = true;
#endif
        bool warningsAsErrors = false;
        std::vector<DX11ShaderMacro> macros;
    };

    struct DX11ShaderBytecodeInfo
    {
        std::string sourceName = "Unknown";
        std::string entryPoint = "Main";
        std::string stage = "Unknown";
        std::string targetProfile = "Unknown";
        std::uint64_t sizeInBytes = 0;
        bool compiledFromFile = false;
        bool hasWarnings = false;
    };

    class DX11ShaderBytecode
    {
    public:
        DX11ShaderBytecode();
        ~DX11ShaderBytecode();

        DX11ShaderBytecode(const DX11ShaderBytecode&) = delete;
        DX11ShaderBytecode& operator=(const DX11ShaderBytecode&) = delete;
        DX11ShaderBytecode(DX11ShaderBytecode&&) noexcept;
        DX11ShaderBytecode& operator=(DX11ShaderBytecode&&) noexcept;

        [[nodiscard]] const DX11ShaderBytecodeInfo& GetInfo() const noexcept;
        [[nodiscard]] std::string_view GetWarnings() const noexcept;
        [[nodiscard]] const void* GetData() const noexcept;
        [[nodiscard]] std::size_t GetSize() const noexcept;
        [[nodiscard]] ID3DBlob* GetNativeBlob() const noexcept;
        [[nodiscard]] bool IsValid() const noexcept;

    private:
        class Impl;

        explicit DX11ShaderBytecode(Scope<Impl> impl);

        friend class DX11ShaderCompiler;
        Scope<Impl> m_impl;
    };

    class DX11ShaderCompiler
    {
    public:
        [[nodiscard]] static DX11ShaderBytecode CompileFromSource(
            std::string_view source,
            DX11ShaderCompileSpecification specification = {});
        [[nodiscard]] static DX11ShaderBytecode CompileFromFile(
            const Path& path,
            DX11ShaderCompileSpecification specification = {});
        [[nodiscard]] static std::string BuildTargetProfile(
            DX11ShaderStage stage,
            std::uint32_t shaderModelMajor = 5,
            std::uint32_t shaderModelMinor = 0);
    };

    [[nodiscard]] std::string_view ToString(DX11ShaderStage stage);
}
