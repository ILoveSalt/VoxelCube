#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <VoxelCube/Core/Base.hpp>
#include <VoxelCube/Renderer/DX11ShaderCompiler.hpp>

struct ID3D11InputLayout;
struct ID3D11RasterizerState;
struct ID3D11BlendState;
struct ID3D11VertexShader;
struct ID3D11PixelShader;

namespace vc
{
    inline constexpr std::uint32_t DX11AppendAlignedElement = 0xFFFFFFFFu;

    enum class DX11InputElementFormat
    {
        Float = 0,
        Float2,
        Float3,
        Float4,
        UInt,
        UInt2,
        UInt4,
        UByte4Normalized
    };

    enum class DX11PrimitiveTopology
    {
        PointList = 0,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip
    };

    enum class DX11FillMode
    {
        Solid = 0,
        Wireframe
    };

    enum class DX11CullMode
    {
        None = 0,
        Front,
        Back
    };

    enum class DX11BlendFactor
    {
        Zero = 0,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DestAlpha,
        InvDestAlpha,
        DestColor,
        InvDestColor
    };

    enum class DX11BlendOperation
    {
        Add = 0,
        Subtract,
        RevSubtract,
        Min,
        Max
    };

    struct DX11InputElement
    {
        std::string semanticName = "POSITION";
        std::uint32_t semanticIndex = 0;
        DX11InputElementFormat format = DX11InputElementFormat::Float3;
        std::uint32_t inputSlot = 0;
        std::uint32_t alignedByteOffset = DX11AppendAlignedElement;
        bool perInstanceData = false;
        std::uint32_t instanceDataStepRate = 0;
    };

    struct DX11RasterizerSpecification
    {
        DX11FillMode fillMode = DX11FillMode::Solid;
        DX11CullMode cullMode = DX11CullMode::Back;
        bool frontCounterClockwise = false;
        bool depthClipEnable = true;
        bool scissorEnable = false;
        bool multisampleEnable = false;
        bool antialiasedLineEnable = false;
    };

    struct DX11BlendSpecification
    {
        bool enableBlending = false;
        DX11BlendFactor sourceColor = DX11BlendFactor::One;
        DX11BlendFactor destinationColor = DX11BlendFactor::Zero;
        DX11BlendOperation colorOperation = DX11BlendOperation::Add;
        DX11BlendFactor sourceAlpha = DX11BlendFactor::One;
        DX11BlendFactor destinationAlpha = DX11BlendFactor::Zero;
        DX11BlendOperation alphaOperation = DX11BlendOperation::Add;
        std::uint8_t writeMask = 0x0F;
    };

    struct DX11Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    struct DX11PipelineStateSpecification
    {
        const DX11ShaderBytecode* vertexShader = nullptr;
        const DX11ShaderBytecode* pixelShader = nullptr;
        std::vector<DX11InputElement> inputElements;
        DX11PrimitiveTopology primitiveTopology = DX11PrimitiveTopology::TriangleList;
        DX11RasterizerSpecification rasterizer;
        DX11BlendSpecification blend;
        DX11Viewport viewport;
    };

    struct DX11PipelineStateInfo
    {
        std::uint32_t inputElementCount = 0;
        std::uint64_t bindCount = 0;
        std::string primitiveTopology = "Unknown";
        std::string fillMode = "Unknown";
        std::string cullMode = "Unknown";
        std::string vertexShaderTarget = "Unknown";
        std::string pixelShaderTarget = "Unknown";
        float viewportWidth = 0.0f;
        float viewportHeight = 0.0f;
        bool alphaBlendingEnabled = false;
        bool ready = false;
    };

    class DX11Device;

    class DX11PipelineState
    {
    public:
        DX11PipelineState(DX11Device& device, DX11PipelineStateSpecification specification = {});
        ~DX11PipelineState();

        DX11PipelineState(const DX11PipelineState&) = delete;
        DX11PipelineState& operator=(const DX11PipelineState&) = delete;
        DX11PipelineState(DX11PipelineState&&) noexcept;
        DX11PipelineState& operator=(DX11PipelineState&&) noexcept;

        [[nodiscard]] const DX11PipelineStateSpecification& GetSpecification() const noexcept;
        [[nodiscard]] const DX11PipelineStateInfo& GetInfo() const noexcept;
        [[nodiscard]] const DX11Viewport& GetViewport() const noexcept;
        [[nodiscard]] ID3D11InputLayout* GetNativeInputLayout() const noexcept;
        [[nodiscard]] ID3D11RasterizerState* GetNativeRasterizerState() const noexcept;
        [[nodiscard]] ID3D11BlendState* GetNativeBlendState() const noexcept;
        [[nodiscard]] ID3D11VertexShader* GetNativeVertexShader() const noexcept;
        [[nodiscard]] ID3D11PixelShader* GetNativePixelShader() const noexcept;

        void Bind();
        void SetViewport(DX11Viewport viewport);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };

    [[nodiscard]] std::string_view ToString(DX11InputElementFormat format);
    [[nodiscard]] std::string_view ToString(DX11PrimitiveTopology topology);
    [[nodiscard]] std::string_view ToString(DX11FillMode fillMode);
    [[nodiscard]] std::string_view ToString(DX11CullMode cullMode);
    [[nodiscard]] std::string_view ToString(DX11BlendFactor blendFactor);
    [[nodiscard]] std::string_view ToString(DX11BlendOperation blendOperation);
}
