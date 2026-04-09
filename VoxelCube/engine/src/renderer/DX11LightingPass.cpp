#include <VoxelCube/Renderer/DX11LightingPass.hpp>

#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

#include <d3d11.h>
#include <wrl/client.h>

#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Buffer.hpp>
#include <VoxelCube/Renderer/DX11Device.hpp>
#include <VoxelCube/Renderer/DX11GBuffer.hpp>
#include <VoxelCube/Renderer/DX11PipelineState.hpp>
#include <VoxelCube/Renderer/DX11RenderTargets.hpp>
#include <VoxelCube/Renderer/DX11SSAOPass.hpp>
#include <VoxelCube/Renderer/DX11ShadowMap.hpp>

namespace
{
    using Microsoft::WRL::ComPtr;

    struct LightingPassGpuParameters
    {
        float directionalLightDirection[3];
        float directionalLightIntensity;
        float directionalLightColor[3];
        float ambientIntensity;
        float pointLightUv[2];
        float pointLightRadius;
        float pointLightIntensity;
        float pointLightColor[3];
        float depthInfluence;
        float aspectRatio;
        float normalStrength;
        float materialInfluence;
        float shadowEnabled;
        float cameraPosition[3];
        float shadowStrength;
        float cameraForward[3];
        float shadowDepthBias;
        float shadowWorldToTextureMatrices[vc::DX11ShadowCascadeCount][16];
        float shadowCascadeData[4];
        float ssaoParameters[4];
    };

    static_assert(sizeof(LightingPassGpuParameters) == 272, "Lighting pass constant buffer must stay 272 bytes.");

    std::string FormatHRESULT(HRESULT result)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
               << static_cast<std::uint32_t>(result);
        return stream.str();
    }

    LightingPassGpuParameters ToGpuParameters(const vc::DX11LightingPassParameters& parameters)
    {
        LightingPassGpuParameters gpuParameters {};

        gpuParameters.directionalLightDirection[0] = parameters.directionalLight.direction[0];
        gpuParameters.directionalLightDirection[1] = parameters.directionalLight.direction[1];
        gpuParameters.directionalLightDirection[2] = parameters.directionalLight.direction[2];
        gpuParameters.directionalLightIntensity = parameters.directionalLight.intensity;

        gpuParameters.directionalLightColor[0] = parameters.directionalLight.color[0];
        gpuParameters.directionalLightColor[1] = parameters.directionalLight.color[1];
        gpuParameters.directionalLightColor[2] = parameters.directionalLight.color[2];
        gpuParameters.ambientIntensity = parameters.directionalLight.ambientIntensity;

        gpuParameters.pointLightUv[0] = parameters.pointLight.screenUv[0];
        gpuParameters.pointLightUv[1] = parameters.pointLight.screenUv[1];
        gpuParameters.pointLightRadius = parameters.pointLight.radius;
        gpuParameters.pointLightIntensity = parameters.pointLight.intensity;

        gpuParameters.pointLightColor[0] = parameters.pointLight.color[0];
        gpuParameters.pointLightColor[1] = parameters.pointLight.color[1];
        gpuParameters.pointLightColor[2] = parameters.pointLight.color[2];
        gpuParameters.depthInfluence = parameters.pointLight.depthInfluence;

        gpuParameters.aspectRatio = parameters.aspectRatio;
        gpuParameters.normalStrength = parameters.normalStrength;
        gpuParameters.materialInfluence = parameters.materialInfluence;
        gpuParameters.shadowEnabled = parameters.shadows.enabled;
        gpuParameters.cameraPosition[0] = parameters.shadows.cameraPosition[0];
        gpuParameters.cameraPosition[1] = parameters.shadows.cameraPosition[1];
        gpuParameters.cameraPosition[2] = parameters.shadows.cameraPosition[2];
        gpuParameters.shadowStrength = parameters.shadows.strength;
        gpuParameters.cameraForward[0] = parameters.shadows.cameraForward[0];
        gpuParameters.cameraForward[1] = parameters.shadows.cameraForward[1];
        gpuParameters.cameraForward[2] = parameters.shadows.cameraForward[2];
        gpuParameters.shadowDepthBias = parameters.shadows.depthBias;

        for (std::size_t cascadeIndex = 0; cascadeIndex < vc::DX11ShadowCascadeCount; ++cascadeIndex)
        {
            for (std::size_t valueIndex = 0; valueIndex < parameters.shadows.cascades[cascadeIndex].worldToShadowTextureMatrix.size(); ++valueIndex)
            {
                gpuParameters.shadowWorldToTextureMatrices[cascadeIndex][valueIndex]
                    = parameters.shadows.cascades[cascadeIndex].worldToShadowTextureMatrix[valueIndex];
            }

            gpuParameters.shadowCascadeData[cascadeIndex] = parameters.shadows.cascades[cascadeIndex].splitDistance;
        }

        gpuParameters.shadowCascadeData[2] = static_cast<float>(parameters.shadows.cascadeCount);
        gpuParameters.shadowCascadeData[3] = parameters.shadows.cascadeBlend;
        gpuParameters.ssaoParameters[0] = parameters.ambientOcclusion.enabled;
        gpuParameters.ssaoParameters[1] = parameters.ambientOcclusion.strength;
        return gpuParameters;
    }
}

namespace vc
{
    class DX11LightingPass::Impl
    {
    public:
        Impl(
            DX11Device& device,
            DX11RenderTargets& renderTargets,
            DX11GBuffer& gBuffer,
            DX11ShadowMap* shadowMap,
            DX11SSAOPass* ssaoPass)
            : m_device(device)
            , m_renderTargets(renderTargets)
            , m_gBuffer(gBuffer)
            , m_shadowMap(shadowMap)
            , m_ssaoPass(ssaoPass)
        {
            CreateSamplerState();
            CreateShadowSamplerState();
            CreateDepthDisabledState();
            CreateConstantBuffer();

            const DX11GBufferInfo& gBufferInfo = m_gBuffer.GetInfo();
            const DX11RenderTargetsInfo& renderTargetsInfo = m_renderTargets.GetInfo();
            m_info.gBufferInputCount = 6;
            m_info.albedoFormat = gBufferInfo.albedoFormat;
            m_info.normalFormat = gBufferInfo.normalFormat;
            m_info.materialFormat = gBufferInfo.materialFormat;
            m_info.depthFormat = renderTargetsInfo.depthShaderResourceFormat;
            m_info.ambientOcclusionFormat = m_ssaoPass != nullptr ? m_ssaoPass->GetInfo().format : "None";
            m_info.samplerFilter = "LinearClamp";
            m_info.shadowSamplerFilter = "ComparisonLinearBorder";
            m_info.shadowsEnabled = (m_shadowMap != nullptr);
            m_info.ambientOcclusionEnabled = (m_ssaoPass != nullptr);
            m_info.shadowCascadeCount = m_shadowMap != nullptr ? m_shadowMap->GetInfo().cascadeCount : 0;

            UpdateParameters({});
            m_info.parameterUpdateCount = 0;
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 lighting pass created: inputs=" +
                std::to_string(m_info.gBufferInputCount) +
                ", albedo=" +
                m_info.albedoFormat +
                ", normal=" +
                m_info.normalFormat +
                ", material=" +
                m_info.materialFormat +
                ", depth=" +
                m_info.depthFormat +
                ", ssao=" +
                std::string(m_info.ambientOcclusionEnabled ? "enabled" : "disabled") +
                ", shadows=" +
                std::string(m_info.shadowsEnabled ? "enabled" : "disabled") +
                ", cascades=" +
                std::to_string(m_info.shadowCascadeCount) +
                ", sampler=" +
                m_info.samplerFilter);
        }

        [[nodiscard]] const DX11LightingPassInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] const DX11LightingPassParameters& GetParameters() const noexcept
        {
            return m_parameters;
        }

        void UpdateParameters(DX11LightingPassParameters parameters)
        {
            VC_ASSERT(m_constantBuffer != nullptr, "DX11 lighting pass constant buffer is not ready.");

            m_parameters = std::move(parameters);
            const LightingPassGpuParameters gpuParameters = ToGpuParameters(m_parameters);
            m_constantBuffer->Write(&gpuParameters, sizeof(gpuParameters));
            ++m_info.parameterUpdateCount;
        }

        void Begin(DX11PipelineState& pipelineState)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 lighting pass requires an immediate context.");

            ID3D11ShaderResourceView* albedoShaderResourceView = m_gBuffer.GetAlbedoShaderResourceView();
            ID3D11ShaderResourceView* normalShaderResourceView = m_gBuffer.GetNormalShaderResourceView();
            ID3D11ShaderResourceView* materialShaderResourceView = m_gBuffer.GetMaterialShaderResourceView();
            ID3D11ShaderResourceView* depthShaderResourceView = m_renderTargets.GetDepthShaderResourceView();
            ID3D11ShaderResourceView* shadowShaderResourceView = m_shadowMap != nullptr
                ? m_shadowMap->GetDepthShaderResourceView()
                : nullptr;
            ID3D11ShaderResourceView* ssaoShaderResourceView = m_ssaoPass != nullptr
                ? m_ssaoPass->GetOcclusionShaderResourceView()
                : nullptr;
            VC_ASSERT(albedoShaderResourceView != nullptr, "DX11 lighting pass requires an albedo SRV.");
            VC_ASSERT(normalShaderResourceView != nullptr, "DX11 lighting pass requires a normal SRV.");
            VC_ASSERT(materialShaderResourceView != nullptr, "DX11 lighting pass requires a material SRV.");
            VC_ASSERT(depthShaderResourceView != nullptr, "DX11 lighting pass requires a depth SRV.");
            if (m_shadowMap != nullptr)
            {
                VC_ASSERT(shadowShaderResourceView != nullptr, "DX11 lighting pass requires a shadow-map SRV.");
            }
            if (m_ssaoPass != nullptr)
            {
                VC_ASSERT(ssaoShaderResourceView != nullptr, "DX11 lighting pass requires an SSAO SRV.");
            }

            m_renderTargets.BindColorOnly();
            pipelineState.Bind();
            immediateContext->OMSetDepthStencilState(m_depthDisabledState.Get(), 0);

            std::array<ID3D11ShaderResourceView*, 6> shaderResourceViews {
                albedoShaderResourceView,
                normalShaderResourceView,
                materialShaderResourceView,
                depthShaderResourceView,
                shadowShaderResourceView,
                ssaoShaderResourceView
            };
            immediateContext->PSSetShaderResources(
                0,
                static_cast<UINT>(shaderResourceViews.size()),
                shaderResourceViews.data());

            ID3D11SamplerState* samplerStates[2] {
                m_samplerState.Get(),
                m_shadowSamplerState.Get()
            };
            immediateContext->PSSetSamplers(0, 2, samplerStates);

            ID3D11Buffer* constantBuffer = m_constantBuffer->GetNativeBuffer();
            VC_ASSERT(constantBuffer != nullptr, "DX11 lighting pass requires a ready constant buffer.");
            immediateContext->PSSetConstantBuffers(0, 1, &constantBuffer);

            ++m_info.passCount;
            ++m_info.inputBindCount;
        }

        void End()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 lighting pass requires an immediate context.");

            std::array<ID3D11ShaderResourceView*, 6> nullShaderResourceViews {};
            immediateContext->PSSetShaderResources(
                0,
                static_cast<UINT>(nullShaderResourceViews.size()),
                nullShaderResourceViews.data());

            ID3D11SamplerState* nullSamplers[2] { nullptr, nullptr };
            immediateContext->PSSetSamplers(0, 2, nullSamplers);

            ID3D11Buffer* nullConstantBuffer = nullptr;
            immediateContext->PSSetConstantBuffers(0, 1, &nullConstantBuffer);
            immediateContext->OMSetDepthStencilState(nullptr, 0);
        }

    private:
        void CreateSamplerState()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 lighting pass requires a ready D3D11 device.");

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
                ("Failed to create DX11 lighting-pass sampler state: " + FormatHRESULT(result)).c_str());
        }

        void CreateShadowSamplerState()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 lighting pass requires a ready D3D11 device.");

            D3D11_SAMPLER_DESC description {};
            description.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            description.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
            description.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
            description.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            description.MipLODBias = 0.0f;
            description.MaxAnisotropy = 1;
            description.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
            description.BorderColor[0] = 1.0f;
            description.BorderColor[1] = 1.0f;
            description.BorderColor[2] = 1.0f;
            description.BorderColor[3] = 1.0f;
            description.MinLOD = 0.0f;
            description.MaxLOD = D3D11_FLOAT32_MAX;

            const HRESULT result = nativeDevice->CreateSamplerState(&description, &m_shadowSamplerState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 lighting-pass shadow sampler state: " + FormatHRESULT(result)).c_str());
        }

        void CreateDepthDisabledState()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 lighting pass requires a ready D3D11 device.");

            D3D11_DEPTH_STENCIL_DESC description {};
            description.DepthEnable = FALSE;
            description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            description.DepthFunc = D3D11_COMPARISON_ALWAYS;
            description.StencilEnable = FALSE;

            const HRESULT result = nativeDevice->CreateDepthStencilState(&description, &m_depthDisabledState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 lighting-pass depth state: " + FormatHRESULT(result)).c_str());
        }

        void CreateConstantBuffer()
        {
            m_constantBuffer = CreateScope<DX11Buffer>(
                m_device,
                DX11BufferSpecification {
                    .sizeInBytes = sizeof(LightingPassGpuParameters),
                    .stride = sizeof(LightingPassGpuParameters),
                    .kind = DX11BufferKind::Constant,
                    .usage = DX11BufferUsage::Dynamic,
                    .cpuReadable = false,
                    .cpuWritable = true
                });
        }

    private:
        DX11Device& m_device;
        DX11RenderTargets& m_renderTargets;
        DX11GBuffer& m_gBuffer;
        DX11ShadowMap* m_shadowMap = nullptr;
        DX11SSAOPass* m_ssaoPass = nullptr;
        DX11LightingPassInfo m_info;
        DX11LightingPassParameters m_parameters;
        Scope<DX11Buffer> m_constantBuffer;
        ComPtr<ID3D11SamplerState> m_samplerState;
        ComPtr<ID3D11SamplerState> m_shadowSamplerState;
        ComPtr<ID3D11DepthStencilState> m_depthDisabledState;
    };

    DX11LightingPass::DX11LightingPass(
        DX11Device& device,
        DX11RenderTargets& renderTargets,
        DX11GBuffer& gBuffer,
        DX11ShadowMap* shadowMap,
        DX11SSAOPass* ssaoPass)
        : m_impl(CreateScope<Impl>(device, renderTargets, gBuffer, shadowMap, ssaoPass))
    {
    }

    DX11LightingPass::~DX11LightingPass() = default;

    DX11LightingPass::DX11LightingPass(DX11LightingPass&& other) noexcept = default;

    DX11LightingPass& DX11LightingPass::operator=(DX11LightingPass&& other) noexcept = default;

    const DX11LightingPassInfo& DX11LightingPass::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    const DX11LightingPassParameters& DX11LightingPass::GetParameters() const noexcept
    {
        return m_impl->GetParameters();
    }

    void DX11LightingPass::UpdateParameters(DX11LightingPassParameters parameters)
    {
        m_impl->UpdateParameters(std::move(parameters));
    }

    void DX11LightingPass::Begin(DX11PipelineState& pipelineState)
    {
        m_impl->Begin(pipelineState);
    }

    void DX11LightingPass::End()
    {
        m_impl->End();
    }
}
