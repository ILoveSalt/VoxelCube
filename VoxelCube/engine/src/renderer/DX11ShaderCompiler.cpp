#include <VoxelCube/Renderer/DX11ShaderCompiler.hpp>

#include <iomanip>
#include <sstream>
#include <utility>

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <VoxelCube/Core/FileSystem.hpp>
#include <VoxelCube/Core/Log.hpp>

namespace
{
    using Microsoft::WRL::ComPtr;

    std::string FormatHRESULT(HRESULT result)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
               << static_cast<std::uint32_t>(result);
        return stream.str();
    }

    std::string ExtractBlobText(ID3DBlob* blob)
    {
        if (blob == nullptr || blob->GetBufferPointer() == nullptr || blob->GetBufferSize() == 0)
        {
            return {};
        }

        std::string text(
            static_cast<const char*>(blob->GetBufferPointer()),
            static_cast<std::size_t>(blob->GetBufferSize()));
        while (!text.empty() && (text.back() == '\0' || text.back() == '\n' || text.back() == '\r'))
        {
            text.pop_back();
        }

        return text;
    }

    UINT BuildCompileFlags(const vc::DX11ShaderCompileSpecification& specification)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;

        if (specification.enableDebugInfo)
        {
            flags |= D3DCOMPILE_DEBUG;
        }

        if (specification.skipOptimization)
        {
            flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
        }
        else
        {
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
        }

        if (specification.warningsAsErrors)
        {
            flags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;
        }

        return flags;
    }

    std::vector<D3D_SHADER_MACRO> BuildMacros(const std::vector<vc::DX11ShaderMacro>& macros)
    {
        std::vector<D3D_SHADER_MACRO> nativeMacros;
        nativeMacros.reserve(macros.size() + 1);

        for (const vc::DX11ShaderMacro& macro : macros)
        {
            nativeMacros.push_back(D3D_SHADER_MACRO {
                macro.name.c_str(),
                macro.value.empty() ? nullptr : macro.value.c_str()
            });
        }

        nativeMacros.push_back(D3D_SHADER_MACRO { nullptr, nullptr });
        return nativeMacros;
    }
}

namespace vc
{
    std::string_view ToString(DX11ShaderStage stage)
    {
        switch (stage)
        {
            case DX11ShaderStage::Vertex:
                return "Vertex";
            case DX11ShaderStage::Pixel:
                return "Pixel";
            case DX11ShaderStage::Geometry:
                return "Geometry";
            case DX11ShaderStage::Hull:
                return "Hull";
            case DX11ShaderStage::Domain:
                return "Domain";
            case DX11ShaderStage::Compute:
                return "Compute";
        }

        return "Unknown";
    }

    class DX11ShaderBytecode::Impl
    {
    public:
        Impl(DX11ShaderBytecodeInfo info, std::string warnings, ComPtr<ID3DBlob> blob)
            : m_info(std::move(info))
            , m_warnings(std::move(warnings))
            , m_blob(std::move(blob))
        {
        }

        [[nodiscard]] const DX11ShaderBytecodeInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] std::string_view GetWarnings() const noexcept
        {
            return m_warnings;
        }

        [[nodiscard]] const void* GetData() const noexcept
        {
            return m_blob != nullptr ? m_blob->GetBufferPointer() : nullptr;
        }

        [[nodiscard]] std::size_t GetSize() const noexcept
        {
            return m_blob != nullptr ? static_cast<std::size_t>(m_blob->GetBufferSize()) : 0;
        }

        [[nodiscard]] ID3DBlob* GetNativeBlob() const noexcept
        {
            return m_blob.Get();
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_blob != nullptr;
        }

    private:
        DX11ShaderBytecodeInfo m_info;
        std::string m_warnings;
        ComPtr<ID3DBlob> m_blob;
    };

    DX11ShaderBytecode::DX11ShaderBytecode() = default;

    DX11ShaderBytecode::DX11ShaderBytecode(Scope<Impl> impl)
        : m_impl(std::move(impl))
    {
    }

    DX11ShaderBytecode::~DX11ShaderBytecode() = default;

    DX11ShaderBytecode::DX11ShaderBytecode(DX11ShaderBytecode&& other) noexcept = default;

    DX11ShaderBytecode& DX11ShaderBytecode::operator=(DX11ShaderBytecode&& other) noexcept = default;

    const DX11ShaderBytecodeInfo& DX11ShaderBytecode::GetInfo() const noexcept
    {
        static const DX11ShaderBytecodeInfo emptyInfo {};
        return m_impl ? m_impl->GetInfo() : emptyInfo;
    }

    std::string_view DX11ShaderBytecode::GetWarnings() const noexcept
    {
        return m_impl ? m_impl->GetWarnings() : std::string_view {};
    }

    const void* DX11ShaderBytecode::GetData() const noexcept
    {
        return m_impl ? m_impl->GetData() : nullptr;
    }

    std::size_t DX11ShaderBytecode::GetSize() const noexcept
    {
        return m_impl ? m_impl->GetSize() : 0;
    }

    ID3DBlob* DX11ShaderBytecode::GetNativeBlob() const noexcept
    {
        return m_impl ? m_impl->GetNativeBlob() : nullptr;
    }

    bool DX11ShaderBytecode::IsValid() const noexcept
    {
        return m_impl ? m_impl->IsValid() : false;
    }

    std::string DX11ShaderCompiler::BuildTargetProfile(
        DX11ShaderStage stage,
        std::uint32_t shaderModelMajor,
        std::uint32_t shaderModelMinor)
    {
        const char* stagePrefix = "vs";
        switch (stage)
        {
            case DX11ShaderStage::Vertex:
                stagePrefix = "vs";
                break;
            case DX11ShaderStage::Pixel:
                stagePrefix = "ps";
                break;
            case DX11ShaderStage::Geometry:
                stagePrefix = "gs";
                break;
            case DX11ShaderStage::Hull:
                stagePrefix = "hs";
                break;
            case DX11ShaderStage::Domain:
                stagePrefix = "ds";
                break;
            case DX11ShaderStage::Compute:
                stagePrefix = "cs";
                break;
        }

        return std::string(stagePrefix) + "_" + std::to_string(shaderModelMajor) + "_" + std::to_string(shaderModelMinor);
    }

    DX11ShaderBytecode DX11ShaderCompiler::CompileFromSource(
        std::string_view source,
        DX11ShaderCompileSpecification specification)
    {
        VC_ASSERT(!source.empty(), "HLSL source must not be empty.");
        if (specification.sourceName.empty())
        {
            specification.sourceName = "InlineShader";
        }

        const std::string targetProfile = BuildTargetProfile(
            specification.stage,
            specification.shaderModelMajor,
            specification.shaderModelMinor);
        const UINT compileFlags = BuildCompileFlags(specification);
        const std::vector<D3D_SHADER_MACRO> macros = BuildMacros(specification.macros);
        const D3D_SHADER_MACRO* nativeMacros = macros.empty() ? nullptr : macros.data();

        ComPtr<ID3DBlob> bytecodeBlob;
        ComPtr<ID3DBlob> diagnosticsBlob;
        const HRESULT result = D3DCompile(
            source.data(),
            source.size(),
            specification.sourceName.c_str(),
            nativeMacros,
            nullptr,
            specification.entryPoint.c_str(),
            targetProfile.c_str(),
            compileFlags,
            0,
            &bytecodeBlob,
            &diagnosticsBlob);

        const std::string diagnostics = ExtractBlobText(diagnosticsBlob.Get());
        VC_ASSERT(
            SUCCEEDED(result),
            ("Failed to compile HLSL source '" +
             specification.sourceName +
             "' [" +
             specification.entryPoint +
             " -> " +
             targetProfile +
             "]: " +
             (diagnostics.empty() ? FormatHRESULT(result) : diagnostics))
                .c_str());

        DX11ShaderBytecodeInfo info {};
        info.sourceName = specification.sourceName;
        info.entryPoint = specification.entryPoint;
        info.stage = std::string(ToString(specification.stage));
        info.targetProfile = targetProfile;
        info.sizeInBytes = bytecodeBlob != nullptr ? bytecodeBlob->GetBufferSize() : 0;
        info.compiledFromFile = false;
        info.hasWarnings = !diagnostics.empty();

        return DX11ShaderBytecode(CreateScope<DX11ShaderBytecode::Impl>(
            std::move(info),
            diagnostics,
            std::move(bytecodeBlob)));
    }

    DX11ShaderBytecode DX11ShaderCompiler::CompileFromFile(
        const Path& path,
        DX11ShaderCompileSpecification specification)
    {
        VC_ASSERT(!path.empty(), "HLSL file path must not be empty.");

        const Path normalizedPath = FileSystem::Normalize(path);
        if (specification.sourceName.empty())
        {
            specification.sourceName = normalizedPath.string();
        }

        const std::string targetProfile = BuildTargetProfile(
            specification.stage,
            specification.shaderModelMajor,
            specification.shaderModelMinor);
        const UINT compileFlags = BuildCompileFlags(specification);
        const std::vector<D3D_SHADER_MACRO> macros = BuildMacros(specification.macros);
        const D3D_SHADER_MACRO* nativeMacros = macros.empty() ? nullptr : macros.data();

        const std::wstring widePath = normalizedPath.wstring();
        ComPtr<ID3DBlob> bytecodeBlob;
        ComPtr<ID3DBlob> diagnosticsBlob;
        const HRESULT result = D3DCompileFromFile(
            widePath.c_str(),
            nativeMacros,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            specification.entryPoint.c_str(),
            targetProfile.c_str(),
            compileFlags,
            0,
            &bytecodeBlob,
            &diagnosticsBlob);

        const std::string diagnostics = ExtractBlobText(diagnosticsBlob.Get());
        VC_ASSERT(
            SUCCEEDED(result),
            ("Failed to compile HLSL file '" +
             normalizedPath.string() +
             "' [" +
             specification.entryPoint +
             " -> " +
             targetProfile +
             "]: " +
             (diagnostics.empty() ? FormatHRESULT(result) : diagnostics))
                .c_str());

        DX11ShaderBytecodeInfo info {};
        info.sourceName = normalizedPath.string();
        info.entryPoint = specification.entryPoint;
        info.stage = std::string(ToString(specification.stage));
        info.targetProfile = targetProfile;
        info.sizeInBytes = bytecodeBlob != nullptr ? bytecodeBlob->GetBufferSize() : 0;
        info.compiledFromFile = true;
        info.hasWarnings = !diagnostics.empty();

        return DX11ShaderBytecode(CreateScope<DX11ShaderBytecode::Impl>(
            std::move(info),
            diagnostics,
            std::move(bytecodeBlob)));
    }
}
