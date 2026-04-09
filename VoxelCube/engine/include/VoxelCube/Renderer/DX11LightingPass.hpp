#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    inline constexpr std::size_t DX11ShadowCascadeCount = 2;

    class DX11Device;
    class DX11GBuffer;
    class DX11PipelineState;
    class DX11RenderTargets;
    class DX11SSAOPass;
    class DX11ShadowMap;

    struct DX11DirectionalLightParameters
    {
        std::array<float, 3> direction { -0.35f, -0.75f, -0.55f };
        float intensity = 1.10f;
        std::array<float, 3> color { 1.00f, 0.95f, 0.88f };
        float ambientIntensity = 0.20f;
    };

    struct DX11PointLightParameters
    {
        std::array<float, 2> screenUv { 0.75f, 0.35f };
        float radius = 0.30f;
        float intensity = 1.15f;
        std::array<float, 3> color { 1.00f, 0.45f, 0.22f };
        float depthInfluence = 0.15f;
    };

    struct DX11LightingPassParameters
    {
        DX11DirectionalLightParameters directionalLight;
        DX11PointLightParameters pointLight;
        struct ShadowCascadeParameters
        {
            std::array<float, 16> worldToShadowTextureMatrix {};
            float splitDistance = 1.0f;
        };

        struct ShadowParameters
        {
            float enabled = 0.0f;
            float strength = 0.72f;
            float depthBias = 0.0025f;
            float cascadeBlend = 0.0f;
            std::array<float, 3> cameraPosition { 0.0f, 0.0f, 0.0f };
            float padding0 = 0.0f;
            std::array<float, 3> cameraForward { 0.0f, 0.0f, 1.0f };
            float padding1 = 0.0f;
            std::array<ShadowCascadeParameters, DX11ShadowCascadeCount> cascades {};
            std::uint32_t cascadeCount = 0;
        };

        struct AmbientOcclusionParameters
        {
            float enabled = 0.0f;
            float strength = 0.85f;
        };

        float aspectRatio = 16.0f / 9.0f;
        float normalStrength = 1.0f;
        float materialInfluence = 0.25f;
        ShadowParameters shadows;
        AmbientOcclusionParameters ambientOcclusion;
    };

    struct DX11LightingPassInfo
    {
        std::uint64_t passCount = 0;
        std::uint64_t inputBindCount = 0;
        std::uint64_t parameterUpdateCount = 0;
        std::uint32_t gBufferInputCount = 0;
        std::string albedoFormat = "Unknown";
        std::string normalFormat = "Unknown";
        std::string materialFormat = "Unknown";
        std::string depthFormat = "Unknown";
        std::string ambientOcclusionFormat = "Unknown";
        std::string samplerFilter = "Unknown";
        std::string shadowSamplerFilter = "Unknown";
        std::uint32_t shadowCascadeCount = 0;
        bool ambientOcclusionEnabled = false;
        bool shadowsEnabled = false;
        bool ready = false;
    };

    class DX11LightingPass
    {
    public:
        DX11LightingPass(
            DX11Device& device,
            DX11RenderTargets& renderTargets,
            DX11GBuffer& gBuffer,
            DX11ShadowMap* shadowMap = nullptr,
            DX11SSAOPass* ssaoPass = nullptr);
        ~DX11LightingPass();

        DX11LightingPass(const DX11LightingPass&) = delete;
        DX11LightingPass& operator=(const DX11LightingPass&) = delete;
        DX11LightingPass(DX11LightingPass&&) noexcept;
        DX11LightingPass& operator=(DX11LightingPass&&) noexcept;

        [[nodiscard]] const DX11LightingPassInfo& GetInfo() const noexcept;
        [[nodiscard]] const DX11LightingPassParameters& GetParameters() const noexcept;

        void UpdateParameters(DX11LightingPassParameters parameters);
        void Begin(DX11PipelineState& pipelineState);
        void End();

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
