#include <VoxelCube/Renderer/DX11PipelineState.hpp>

#include <iomanip>
#include <sstream>
#include <utility>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Device.hpp>

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

    DXGI_FORMAT ToNativeFormat(vc::DX11InputElementFormat format)
    {
        switch (format)
        {
            case vc::DX11InputElementFormat::Float:
                return DXGI_FORMAT_R32_FLOAT;
            case vc::DX11InputElementFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case vc::DX11InputElementFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case vc::DX11InputElementFormat::Float4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case vc::DX11InputElementFormat::UInt:
                return DXGI_FORMAT_R32_UINT;
            case vc::DX11InputElementFormat::UInt2:
                return DXGI_FORMAT_R32G32_UINT;
            case vc::DX11InputElementFormat::UInt4:
                return DXGI_FORMAT_R32G32B32A32_UINT;
            case vc::DX11InputElementFormat::UByte4Normalized:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        return DXGI_FORMAT_UNKNOWN;
    }

    D3D11_INPUT_CLASSIFICATION ToNativeInputClassification(const vc::DX11InputElement& element)
    {
        return element.perInstanceData ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
    }

    D3D11_PRIMITIVE_TOPOLOGY ToNativePrimitiveTopology(vc::DX11PrimitiveTopology topology)
    {
        switch (topology)
        {
            case vc::DX11PrimitiveTopology::PointList:
                return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            case vc::DX11PrimitiveTopology::LineList:
                return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case vc::DX11PrimitiveTopology::LineStrip:
                return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
            case vc::DX11PrimitiveTopology::TriangleList:
                return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case vc::DX11PrimitiveTopology::TriangleStrip:
                return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        }

        return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    }

    D3D11_FILL_MODE ToNativeFillMode(vc::DX11FillMode fillMode)
    {
        switch (fillMode)
        {
            case vc::DX11FillMode::Solid:
                return D3D11_FILL_SOLID;
            case vc::DX11FillMode::Wireframe:
                return D3D11_FILL_WIREFRAME;
        }

        return D3D11_FILL_SOLID;
    }

    D3D11_CULL_MODE ToNativeCullMode(vc::DX11CullMode cullMode)
    {
        switch (cullMode)
        {
            case vc::DX11CullMode::None:
                return D3D11_CULL_NONE;
            case vc::DX11CullMode::Front:
                return D3D11_CULL_FRONT;
            case vc::DX11CullMode::Back:
                return D3D11_CULL_BACK;
        }

        return D3D11_CULL_BACK;
    }

    D3D11_BLEND ToNativeBlendFactor(vc::DX11BlendFactor blendFactor)
    {
        switch (blendFactor)
        {
            case vc::DX11BlendFactor::Zero:
                return D3D11_BLEND_ZERO;
            case vc::DX11BlendFactor::One:
                return D3D11_BLEND_ONE;
            case vc::DX11BlendFactor::SrcColor:
                return D3D11_BLEND_SRC_COLOR;
            case vc::DX11BlendFactor::InvSrcColor:
                return D3D11_BLEND_INV_SRC_COLOR;
            case vc::DX11BlendFactor::SrcAlpha:
                return D3D11_BLEND_SRC_ALPHA;
            case vc::DX11BlendFactor::InvSrcAlpha:
                return D3D11_BLEND_INV_SRC_ALPHA;
            case vc::DX11BlendFactor::DestAlpha:
                return D3D11_BLEND_DEST_ALPHA;
            case vc::DX11BlendFactor::InvDestAlpha:
                return D3D11_BLEND_INV_DEST_ALPHA;
            case vc::DX11BlendFactor::DestColor:
                return D3D11_BLEND_DEST_COLOR;
            case vc::DX11BlendFactor::InvDestColor:
                return D3D11_BLEND_INV_DEST_COLOR;
        }

        return D3D11_BLEND_ONE;
    }

    D3D11_BLEND_OP ToNativeBlendOperation(vc::DX11BlendOperation blendOperation)
    {
        switch (blendOperation)
        {
            case vc::DX11BlendOperation::Add:
                return D3D11_BLEND_OP_ADD;
            case vc::DX11BlendOperation::Subtract:
                return D3D11_BLEND_OP_SUBTRACT;
            case vc::DX11BlendOperation::RevSubtract:
                return D3D11_BLEND_OP_REV_SUBTRACT;
            case vc::DX11BlendOperation::Min:
                return D3D11_BLEND_OP_MIN;
            case vc::DX11BlendOperation::Max:
                return D3D11_BLEND_OP_MAX;
        }

        return D3D11_BLEND_OP_ADD;
    }

    D3D11_VIEWPORT ToNativeViewport(const vc::DX11Viewport& viewport)
    {
        D3D11_VIEWPORT nativeViewport {};
        nativeViewport.TopLeftX = viewport.x;
        nativeViewport.TopLeftY = viewport.y;
        nativeViewport.Width = viewport.width;
        nativeViewport.Height = viewport.height;
        nativeViewport.MinDepth = viewport.minDepth;
        nativeViewport.MaxDepth = viewport.maxDepth;
        return nativeViewport;
    }
}

namespace vc
{
    std::string_view ToString(DX11InputElementFormat format)
    {
        switch (format)
        {
            case DX11InputElementFormat::Float:
                return "Float";
            case DX11InputElementFormat::Float2:
                return "Float2";
            case DX11InputElementFormat::Float3:
                return "Float3";
            case DX11InputElementFormat::Float4:
                return "Float4";
            case DX11InputElementFormat::UInt:
                return "UInt";
            case DX11InputElementFormat::UInt2:
                return "UInt2";
            case DX11InputElementFormat::UInt4:
                return "UInt4";
            case DX11InputElementFormat::UByte4Normalized:
                return "UByte4Normalized";
        }

        return "Unknown";
    }

    std::string_view ToString(DX11PrimitiveTopology topology)
    {
        switch (topology)
        {
            case DX11PrimitiveTopology::PointList:
                return "PointList";
            case DX11PrimitiveTopology::LineList:
                return "LineList";
            case DX11PrimitiveTopology::LineStrip:
                return "LineStrip";
            case DX11PrimitiveTopology::TriangleList:
                return "TriangleList";
            case DX11PrimitiveTopology::TriangleStrip:
                return "TriangleStrip";
        }

        return "Unknown";
    }

    std::string_view ToString(DX11FillMode fillMode)
    {
        switch (fillMode)
        {
            case DX11FillMode::Solid:
                return "Solid";
            case DX11FillMode::Wireframe:
                return "Wireframe";
        }

        return "Unknown";
    }

    std::string_view ToString(DX11CullMode cullMode)
    {
        switch (cullMode)
        {
            case DX11CullMode::None:
                return "None";
            case DX11CullMode::Front:
                return "Front";
            case DX11CullMode::Back:
                return "Back";
        }

        return "Unknown";
    }

    std::string_view ToString(DX11BlendFactor blendFactor)
    {
        switch (blendFactor)
        {
            case DX11BlendFactor::Zero:
                return "Zero";
            case DX11BlendFactor::One:
                return "One";
            case DX11BlendFactor::SrcColor:
                return "SrcColor";
            case DX11BlendFactor::InvSrcColor:
                return "InvSrcColor";
            case DX11BlendFactor::SrcAlpha:
                return "SrcAlpha";
            case DX11BlendFactor::InvSrcAlpha:
                return "InvSrcAlpha";
            case DX11BlendFactor::DestAlpha:
                return "DestAlpha";
            case DX11BlendFactor::InvDestAlpha:
                return "InvDestAlpha";
            case DX11BlendFactor::DestColor:
                return "DestColor";
            case DX11BlendFactor::InvDestColor:
                return "InvDestColor";
        }

        return "Unknown";
    }

    std::string_view ToString(DX11BlendOperation blendOperation)
    {
        switch (blendOperation)
        {
            case DX11BlendOperation::Add:
                return "Add";
            case DX11BlendOperation::Subtract:
                return "Subtract";
            case DX11BlendOperation::RevSubtract:
                return "RevSubtract";
            case DX11BlendOperation::Min:
                return "Min";
            case DX11BlendOperation::Max:
                return "Max";
        }

        return "Unknown";
    }

    class DX11PipelineState::Impl
    {
    public:
        Impl(DX11Device& device, DX11PipelineStateSpecification specification)
            : m_device(device)
            , m_specification(std::move(specification))
        {
            ValidateSpecification();
            CreateShaders();
            CreateInputLayout();
            CreateRasterizerState();
            CreateBlendState();
            UpdateViewportInfo(m_specification.viewport, false);
            m_info.inputElementCount = static_cast<std::uint32_t>(m_specification.inputElements.size());
            m_info.primitiveTopology = std::string(ToString(m_specification.primitiveTopology));
            m_info.fillMode = std::string(ToString(m_specification.rasterizer.fillMode));
            m_info.cullMode = std::string(ToString(m_specification.rasterizer.cullMode));
            m_info.alphaBlendingEnabled = m_specification.blend.enableBlending;
            m_info.vertexShaderTarget = m_specification.vertexShader->GetInfo().targetProfile;
            m_info.pixelShaderTarget = m_specification.pixelShader->GetInfo().targetProfile;
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 pipeline state created: topology=" +
                m_info.primitiveTopology +
                ", inputElements=" +
                std::to_string(m_info.inputElementCount) +
                ", rasterizer=" +
                m_info.fillMode +
                "/" +
                m_info.cullMode +
                ", blending=" +
                std::string(m_info.alphaBlendingEnabled ? "enabled" : "disabled") +
                ", viewport=" +
                std::to_string(static_cast<std::uint32_t>(m_info.viewportWidth)) +
                "x" +
                std::to_string(static_cast<std::uint32_t>(m_info.viewportHeight)) +
                ", VS=" +
                m_info.vertexShaderTarget +
                ", PS=" +
                m_info.pixelShaderTarget);
        }

        [[nodiscard]] const DX11PipelineStateSpecification& GetSpecification() const noexcept
        {
            return m_specification;
        }

        [[nodiscard]] const DX11PipelineStateInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] const DX11Viewport& GetViewport() const noexcept
        {
            return m_specification.viewport;
        }

        [[nodiscard]] ID3D11InputLayout* GetNativeInputLayout() const noexcept
        {
            return m_inputLayout.Get();
        }

        [[nodiscard]] ID3D11RasterizerState* GetNativeRasterizerState() const noexcept
        {
            return m_rasterizerState.Get();
        }

        [[nodiscard]] ID3D11BlendState* GetNativeBlendState() const noexcept
        {
            return m_blendState.Get();
        }

        [[nodiscard]] ID3D11VertexShader* GetNativeVertexShader() const noexcept
        {
            return m_vertexShader.Get();
        }

        [[nodiscard]] ID3D11PixelShader* GetNativePixelShader() const noexcept
        {
            return m_pixelShader.Get();
        }

        void Bind()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 pipeline state requires an immediate context.");

            immediateContext->IASetPrimitiveTopology(ToNativePrimitiveTopology(m_specification.primitiveTopology));
            immediateContext->IASetInputLayout(m_inputLayout.Get());
            immediateContext->VSSetShader(m_vertexShader.Get(), nullptr, 0);
            immediateContext->PSSetShader(m_pixelShader.Get(), nullptr, 0);
            immediateContext->RSSetState(m_rasterizerState.Get());

            const D3D11_VIEWPORT viewport = ToNativeViewport(m_specification.viewport);
            immediateContext->RSSetViewports(1, &viewport);

            constexpr float kBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            immediateContext->OMSetBlendState(m_blendState.Get(), kBlendFactor, 0xFFFFFFFFu);
            ++m_info.bindCount;
        }

        void SetViewport(DX11Viewport viewport)
        {
            UpdateViewportInfo(std::move(viewport), true);
        }

    private:
        void ValidateSpecification() const
        {
            VC_ASSERT(m_specification.vertexShader != nullptr, "DX11 pipeline state requires a vertex shader bytecode.");
            VC_ASSERT(m_specification.pixelShader != nullptr, "DX11 pipeline state requires a pixel shader bytecode.");
            VC_ASSERT(m_specification.vertexShader->IsValid(), "DX11 pipeline state received an invalid vertex shader bytecode.");
            VC_ASSERT(m_specification.pixelShader->IsValid(), "DX11 pipeline state received an invalid pixel shader bytecode.");
            VC_ASSERT(
                m_specification.vertexShader->GetInfo().stage == "Vertex",
                "DX11 pipeline state vertex shader bytecode must come from the Vertex stage.");
            VC_ASSERT(
                m_specification.pixelShader->GetInfo().stage == "Pixel",
                "DX11 pipeline state pixel shader bytecode must come from the Pixel stage.");
            VC_ASSERT(m_specification.viewport.width > 0.0f, "DX11 pipeline viewport width must be greater than zero.");
            VC_ASSERT(m_specification.viewport.height > 0.0f, "DX11 pipeline viewport height must be greater than zero.");

            for (const DX11InputElement& element : m_specification.inputElements)
            {
                VC_ASSERT(!element.semanticName.empty(), "DX11 input element semantic name must not be empty.");
                if (element.perInstanceData)
                {
                    VC_ASSERT(
                        element.instanceDataStepRate > 0,
                        "DX11 per-instance input elements require instanceDataStepRate > 0.");
                }
            }
        }

        void CreateShaders()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 pipeline state requires a ready D3D11 device.");

            HRESULT result = nativeDevice->CreateVertexShader(
                m_specification.vertexShader->GetData(),
                m_specification.vertexShader->GetSize(),
                nullptr,
                &m_vertexShader);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 vertex shader: " + FormatHRESULT(result)).c_str());

            result = nativeDevice->CreatePixelShader(
                m_specification.pixelShader->GetData(),
                m_specification.pixelShader->GetSize(),
                nullptr,
                &m_pixelShader);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 pixel shader: " + FormatHRESULT(result)).c_str());
        }

        void CreateInputLayout()
        {
            if (m_specification.inputElements.empty())
            {
                return;
            }

            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 pipeline state requires a ready D3D11 device.");

            std::vector<D3D11_INPUT_ELEMENT_DESC> nativeElements;
            nativeElements.reserve(m_specification.inputElements.size());

            for (const DX11InputElement& element : m_specification.inputElements)
            {
                nativeElements.push_back(D3D11_INPUT_ELEMENT_DESC {
                    element.semanticName.c_str(),
                    element.semanticIndex,
                    ToNativeFormat(element.format),
                    element.inputSlot,
                    element.alignedByteOffset,
                    ToNativeInputClassification(element),
                    element.instanceDataStepRate
                });
            }

            const HRESULT result = nativeDevice->CreateInputLayout(
                nativeElements.data(),
                static_cast<UINT>(nativeElements.size()),
                m_specification.vertexShader->GetData(),
                m_specification.vertexShader->GetSize(),
                &m_inputLayout);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 input layout: " + FormatHRESULT(result)).c_str());
        }

        void CreateRasterizerState()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 pipeline state requires a ready D3D11 device.");

            D3D11_RASTERIZER_DESC description {};
            description.FillMode = ToNativeFillMode(m_specification.rasterizer.fillMode);
            description.CullMode = ToNativeCullMode(m_specification.rasterizer.cullMode);
            description.FrontCounterClockwise = m_specification.rasterizer.frontCounterClockwise;
            description.DepthBias = 0;
            description.DepthBiasClamp = 0.0f;
            description.SlopeScaledDepthBias = 0.0f;
            description.DepthClipEnable = m_specification.rasterizer.depthClipEnable;
            description.ScissorEnable = m_specification.rasterizer.scissorEnable;
            description.MultisampleEnable = m_specification.rasterizer.multisampleEnable;
            description.AntialiasedLineEnable = m_specification.rasterizer.antialiasedLineEnable;

            const HRESULT result = nativeDevice->CreateRasterizerState(&description, &m_rasterizerState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 rasterizer state: " + FormatHRESULT(result)).c_str());
        }

        void CreateBlendState()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 pipeline state requires a ready D3D11 device.");

            D3D11_BLEND_DESC description {};
            description.AlphaToCoverageEnable = FALSE;
            description.IndependentBlendEnable = FALSE;

            D3D11_RENDER_TARGET_BLEND_DESC& renderTarget = description.RenderTarget[0];
            renderTarget.BlendEnable = m_specification.blend.enableBlending;
            renderTarget.SrcBlend = ToNativeBlendFactor(m_specification.blend.sourceColor);
            renderTarget.DestBlend = ToNativeBlendFactor(m_specification.blend.destinationColor);
            renderTarget.BlendOp = ToNativeBlendOperation(m_specification.blend.colorOperation);
            renderTarget.SrcBlendAlpha = ToNativeBlendFactor(m_specification.blend.sourceAlpha);
            renderTarget.DestBlendAlpha = ToNativeBlendFactor(m_specification.blend.destinationAlpha);
            renderTarget.BlendOpAlpha = ToNativeBlendOperation(m_specification.blend.alphaOperation);
            renderTarget.RenderTargetWriteMask = m_specification.blend.writeMask;

            const HRESULT result = nativeDevice->CreateBlendState(&description, &m_blendState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 blend state: " + FormatHRESULT(result)).c_str());
        }

        void UpdateViewportInfo(DX11Viewport viewport, bool emitLog)
        {
            VC_ASSERT(viewport.width > 0.0f, "DX11 pipeline viewport width must be greater than zero.");
            VC_ASSERT(viewport.height > 0.0f, "DX11 pipeline viewport height must be greater than zero.");

            const float previousWidth = m_specification.viewport.width;
            const float previousHeight = m_specification.viewport.height;
            m_specification.viewport = std::move(viewport);
            m_info.viewportWidth = m_specification.viewport.width;
            m_info.viewportHeight = m_specification.viewport.height;

            if (emitLog
                && (previousWidth != m_info.viewportWidth || previousHeight != m_info.viewportHeight))
            {
                VC_LOG_INFO(
                    "DX11 pipeline viewport updated: " +
                    std::to_string(static_cast<std::uint32_t>(m_info.viewportWidth)) +
                    "x" +
                    std::to_string(static_cast<std::uint32_t>(m_info.viewportHeight)));
            }
        }

    private:
        DX11Device& m_device;
        DX11PipelineStateSpecification m_specification;
        DX11PipelineStateInfo m_info;
        ComPtr<ID3D11InputLayout> m_inputLayout;
        ComPtr<ID3D11RasterizerState> m_rasterizerState;
        ComPtr<ID3D11BlendState> m_blendState;
        ComPtr<ID3D11VertexShader> m_vertexShader;
        ComPtr<ID3D11PixelShader> m_pixelShader;
    };

    DX11PipelineState::DX11PipelineState(DX11Device& device, DX11PipelineStateSpecification specification)
        : m_impl(CreateScope<Impl>(device, std::move(specification)))
    {
    }

    DX11PipelineState::~DX11PipelineState() = default;

    DX11PipelineState::DX11PipelineState(DX11PipelineState&& other) noexcept = default;

    DX11PipelineState& DX11PipelineState::operator=(DX11PipelineState&& other) noexcept = default;

    const DX11PipelineStateSpecification& DX11PipelineState::GetSpecification() const noexcept
    {
        return m_impl->GetSpecification();
    }

    const DX11PipelineStateInfo& DX11PipelineState::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    const DX11Viewport& DX11PipelineState::GetViewport() const noexcept
    {
        return m_impl->GetViewport();
    }

    ID3D11InputLayout* DX11PipelineState::GetNativeInputLayout() const noexcept
    {
        return m_impl->GetNativeInputLayout();
    }

    ID3D11RasterizerState* DX11PipelineState::GetNativeRasterizerState() const noexcept
    {
        return m_impl->GetNativeRasterizerState();
    }

    ID3D11BlendState* DX11PipelineState::GetNativeBlendState() const noexcept
    {
        return m_impl->GetNativeBlendState();
    }

    ID3D11VertexShader* DX11PipelineState::GetNativeVertexShader() const noexcept
    {
        return m_impl->GetNativeVertexShader();
    }

    ID3D11PixelShader* DX11PipelineState::GetNativePixelShader() const noexcept
    {
        return m_impl->GetNativePixelShader();
    }

    void DX11PipelineState::Bind()
    {
        m_impl->Bind();
    }

    void DX11PipelineState::SetViewport(DX11Viewport viewport)
    {
        m_impl->SetViewport(std::move(viewport));
    }
}
