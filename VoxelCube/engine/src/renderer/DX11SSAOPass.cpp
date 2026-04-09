#include <VoxelCube/Renderer/DX11SSAOPass.hpp>

#include <array>
#include <iomanip>
#include <sstream>
#include <utility>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Buffer.hpp>
#include <VoxelCube/Renderer/DX11Device.hpp>
#include <VoxelCube/Renderer/DX11GBuffer.hpp>
#include <VoxelCube/Renderer/DX11PipelineState.hpp>
#include <VoxelCube/Renderer/DX11RenderTargets.hpp>

namespace
{
    using Microsoft::WRL::ComPtr;

    struct SsaoPassGpuParameters
    {
        float inverseResolution[2];
        float sampleRadius;
        float worldRadius;
        float intensity;
        float power;
        float normalBias;
        float rotation;
    };

    static_assert(sizeof(SsaoPassGpuParameters) == 32, "SSAO pass constant buffer must stay 32 bytes.");

    std::string FormatHRESULT(HRESULT result)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
               << static_cast<std::uint32_t>(result);
        return stream.str();
    }

    std::string FormatToString(DXGI_FORMAT format)
    {
        switch (format)
        {
            case DXGI_FORMAT_R8_UNORM:
                return "R8_UNORM";
            default:
                return "Unknown";
        }
    }

    SsaoPassGpuParameters ToGpuParameters(const vc::DX11SSAOPassParameters& parameters)
    {
        SsaoPassGpuParameters gpuParameters {};
        gpuParameters.inverseResolution[0] = parameters.inverseResolution[0];
        gpuParameters.inverseResolution[1] = parameters.inverseResolution[1];
        gpuParameters.sampleRadius = parameters.sampleRadius;
        gpuParameters.worldRadius = parameters.worldRadius;
        gpuParameters.intensity = parameters.intensity;
        gpuParameters.power = parameters.power;
        gpuParameters.normalBias = parameters.normalBias;
        gpuParameters.rotation = parameters.rotation;
        return gpuParameters;
    }
}

namespace vc
{
    class DX11SSAOPass::Impl
    {
    public:
        Impl(DX11Device& device, DX11RenderTargets& renderTargets, DX11GBuffer& gBuffer)
            : m_device(device)
            , m_renderTargets(renderTargets)
            , m_gBuffer(gBuffer)
        {
            const DX11RenderTargetsInfo& renderTargetsInfo = m_renderTargets.GetInfo();
            CreateDepthDisabledState();
            CreateSamplerState();
            CreateConstantBuffer();
            Recreate(renderTargetsInfo.width, renderTargetsInfo.height);
            UpdateParameters({});
            m_info.parameterUpdateCount = 0;
            m_info.sampleCount = 8;
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 SSAO pass created: size=" +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height) +
                ", format=" +
                m_info.format +
                ", samples=" +
                std::to_string(m_info.sampleCount));
        }

        [[nodiscard]] const DX11SSAOPassInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] const DX11SSAOPassParameters& GetParameters() const noexcept
        {
            return m_parameters;
        }

        [[nodiscard]] ID3D11Texture2D* GetOcclusionTexture() const noexcept
        {
            return m_occlusionTexture.Get();
        }

        [[nodiscard]] ID3D11RenderTargetView* GetOcclusionRenderTargetView() const noexcept
        {
            return m_occlusionRenderTargetView.Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* GetOcclusionShaderResourceView() const noexcept
        {
            return m_occlusionShaderResourceView.Get();
        }

        void Resize(std::uint32_t width, std::uint32_t height)
        {
            if (width == 0 || height == 0)
            {
                return;
            }

            if (m_info.width == width && m_info.height == height)
            {
                return;
            }

            ReleaseResources();
            Recreate(width, height);
            DX11SSAOPassParameters parameters = m_parameters;
            parameters.inverseResolution = {
                1.0f / static_cast<float>(m_info.width),
                1.0f / static_cast<float>(m_info.height)
            };
            UpdateParameters(std::move(parameters));

            VC_LOG_INFO(
                "DX11 SSAO pass resized: " +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height));
        }

        void Clear(float occlusion)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 SSAO pass requires an immediate context.");

            const float clampedOcclusion = std::clamp(occlusion, 0.0f, 1.0f);
            const std::array<float, 4> clearColor {
                clampedOcclusion,
                clampedOcclusion,
                clampedOcclusion,
                1.0f
            };
            immediateContext->ClearRenderTargetView(m_occlusionRenderTargetView.Get(), clearColor.data());
            ++m_info.clearCount;
        }

        void UpdateParameters(DX11SSAOPassParameters parameters)
        {
            VC_ASSERT(m_constantBuffer != nullptr, "DX11 SSAO constant buffer is not ready.");

            m_parameters = std::move(parameters);
            const SsaoPassGpuParameters gpuParameters = ToGpuParameters(m_parameters);
            m_constantBuffer->Write(&gpuParameters, sizeof(gpuParameters));
            ++m_info.parameterUpdateCount;
        }

        void Begin(DX11PipelineState& pipelineState)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 SSAO pass requires an immediate context.");

            ID3D11ShaderResourceView* normalShaderResourceView = m_gBuffer.GetNormalShaderResourceView();
            ID3D11ShaderResourceView* depthShaderResourceView = m_renderTargets.GetDepthShaderResourceView();
            ID3D11ShaderResourceView* materialShaderResourceView = m_gBuffer.GetMaterialShaderResourceView();
            VC_ASSERT(normalShaderResourceView != nullptr, "DX11 SSAO pass requires a normal SRV.");
            VC_ASSERT(depthShaderResourceView != nullptr, "DX11 SSAO pass requires a depth SRV.");
            VC_ASSERT(materialShaderResourceView != nullptr, "DX11 SSAO pass requires a material SRV.");

            ID3D11RenderTargetView* renderTargetView = m_occlusionRenderTargetView.Get();
            immediateContext->OMSetRenderTargets(1, &renderTargetView, nullptr);
            pipelineState.Bind();
            immediateContext->OMSetDepthStencilState(m_depthDisabledState.Get(), 0);

            std::array<ID3D11ShaderResourceView*, 3> shaderResourceViews {
                normalShaderResourceView,
                depthShaderResourceView,
                materialShaderResourceView
            };
            immediateContext->PSSetShaderResources(
                0,
                static_cast<UINT>(shaderResourceViews.size()),
                shaderResourceViews.data());

            ID3D11SamplerState* samplerState = m_samplerState.Get();
            immediateContext->PSSetSamplers(0, 1, &samplerState);

            ID3D11Buffer* constantBuffer = m_constantBuffer->GetNativeBuffer();
            VC_ASSERT(constantBuffer != nullptr, "DX11 SSAO pass requires a ready constant buffer.");
            immediateContext->PSSetConstantBuffers(0, 1, &constantBuffer);

            ++m_info.passCount;
            ++m_info.inputBindCount;
        }

        void End()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 SSAO pass requires an immediate context.");

            std::array<ID3D11ShaderResourceView*, 3> nullShaderResourceViews {};
            immediateContext->PSSetShaderResources(
                0,
                static_cast<UINT>(nullShaderResourceViews.size()),
                nullShaderResourceViews.data());

            ID3D11SamplerState* nullSamplerState = nullptr;
            immediateContext->PSSetSamplers(0, 1, &nullSamplerState);

            ID3D11Buffer* nullConstantBuffer = nullptr;
            immediateContext->PSSetConstantBuffers(0, 1, &nullConstantBuffer);
            immediateContext->OMSetDepthStencilState(nullptr, 0);
        }

    private:
        void CreateDepthDisabledState()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 SSAO pass requires a ready D3D11 device.");

            D3D11_DEPTH_STENCIL_DESC description {};
            description.DepthEnable = FALSE;
            description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            description.DepthFunc = D3D11_COMPARISON_ALWAYS;
            description.StencilEnable = FALSE;

            const HRESULT result = nativeDevice->CreateDepthStencilState(&description, &m_depthDisabledState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 SSAO depth state: " + FormatHRESULT(result)).c_str());
        }

        void CreateSamplerState()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 SSAO pass requires a ready D3D11 device.");

            D3D11_SAMPLER_DESC description {};
            description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            description.MipLODBias = 0.0f;
            description.MaxAnisotropy = 1;
            description.ComparisonFunc = D3D11_COMPARISON_NEVER;
            description.BorderColor[0] = 0.0f;
            description.BorderColor[1] = 0.0f;
            description.BorderColor[2] = 0.0f;
            description.BorderColor[3] = 0.0f;
            description.MinLOD = 0.0f;
            description.MaxLOD = D3D11_FLOAT32_MAX;

            const HRESULT result = nativeDevice->CreateSamplerState(&description, &m_samplerState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 SSAO sampler state: " + FormatHRESULT(result)).c_str());
        }

        void CreateConstantBuffer()
        {
            m_constantBuffer = CreateScope<DX11Buffer>(
                m_device,
                DX11BufferSpecification {
                    .sizeInBytes = sizeof(SsaoPassGpuParameters),
                    .stride = sizeof(SsaoPassGpuParameters),
                    .kind = DX11BufferKind::Constant,
                    .usage = DX11BufferUsage::Dynamic,
                    .cpuReadable = false,
                    .cpuWritable = true
                });
        }

        void ReleaseResources()
        {
            m_occlusionShaderResourceView.Reset();
            m_occlusionRenderTargetView.Reset();
            m_occlusionTexture.Reset();
            m_info.ready = false;
        }

        void Recreate(std::uint32_t width, std::uint32_t height)
        {
            VC_ASSERT(width > 0, "DX11 SSAO width must be greater than zero.");
            VC_ASSERT(height > 0, "DX11 SSAO height must be greater than zero.");

            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 SSAO pass requires a ready D3D11 device.");

            D3D11_TEXTURE2D_DESC textureDescription {};
            textureDescription.Width = width;
            textureDescription.Height = height;
            textureDescription.MipLevels = 1;
            textureDescription.ArraySize = 1;
            textureDescription.Format = DXGI_FORMAT_R8_UNORM;
            textureDescription.SampleDesc.Count = 1;
            textureDescription.SampleDesc.Quality = 0;
            textureDescription.Usage = D3D11_USAGE_DEFAULT;
            textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            textureDescription.CPUAccessFlags = 0;
            textureDescription.MiscFlags = 0;

            HRESULT result = nativeDevice->CreateTexture2D(&textureDescription, nullptr, &m_occlusionTexture);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 SSAO texture: " + FormatHRESULT(result)).c_str());

            result = nativeDevice->CreateRenderTargetView(
                m_occlusionTexture.Get(),
                nullptr,
                &m_occlusionRenderTargetView);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 SSAO render-target view: " + FormatHRESULT(result)).c_str());

            result = nativeDevice->CreateShaderResourceView(
                m_occlusionTexture.Get(),
                nullptr,
                &m_occlusionShaderResourceView);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 SSAO shader-resource view: " + FormatHRESULT(result)).c_str());

            m_info.width = width;
            m_info.height = height;
            m_info.format = FormatToString(textureDescription.Format);
            m_info.ready = true;
        }

    private:
        DX11Device& m_device;
        DX11RenderTargets& m_renderTargets;
        DX11GBuffer& m_gBuffer;
        DX11SSAOPassInfo m_info;
        DX11SSAOPassParameters m_parameters;
        Scope<DX11Buffer> m_constantBuffer;
        ComPtr<ID3D11Texture2D> m_occlusionTexture;
        ComPtr<ID3D11RenderTargetView> m_occlusionRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> m_occlusionShaderResourceView;
        ComPtr<ID3D11SamplerState> m_samplerState;
        ComPtr<ID3D11DepthStencilState> m_depthDisabledState;
    };

    DX11SSAOPass::DX11SSAOPass(DX11Device& device, DX11RenderTargets& renderTargets, DX11GBuffer& gBuffer)
        : m_impl(CreateScope<Impl>(device, renderTargets, gBuffer))
    {
    }

    DX11SSAOPass::~DX11SSAOPass() = default;

    DX11SSAOPass::DX11SSAOPass(DX11SSAOPass&& other) noexcept = default;

    DX11SSAOPass& DX11SSAOPass::operator=(DX11SSAOPass&& other) noexcept = default;

    const DX11SSAOPassInfo& DX11SSAOPass::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    const DX11SSAOPassParameters& DX11SSAOPass::GetParameters() const noexcept
    {
        return m_impl->GetParameters();
    }

    ID3D11Texture2D* DX11SSAOPass::GetOcclusionTexture() const noexcept
    {
        return m_impl->GetOcclusionTexture();
    }

    ID3D11RenderTargetView* DX11SSAOPass::GetOcclusionRenderTargetView() const noexcept
    {
        return m_impl->GetOcclusionRenderTargetView();
    }

    ID3D11ShaderResourceView* DX11SSAOPass::GetOcclusionShaderResourceView() const noexcept
    {
        return m_impl->GetOcclusionShaderResourceView();
    }

    void DX11SSAOPass::Resize(std::uint32_t width, std::uint32_t height)
    {
        m_impl->Resize(width, height);
    }

    void DX11SSAOPass::Clear(float occlusion)
    {
        m_impl->Clear(occlusion);
    }

    void DX11SSAOPass::UpdateParameters(DX11SSAOPassParameters parameters)
    {
        m_impl->UpdateParameters(std::move(parameters));
    }

    void DX11SSAOPass::Begin(DX11PipelineState& pipelineState)
    {
        m_impl->Begin(pipelineState);
    }

    void DX11SSAOPass::End()
    {
        m_impl->End();
    }
}
