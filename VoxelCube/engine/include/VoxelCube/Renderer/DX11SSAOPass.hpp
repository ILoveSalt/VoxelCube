#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace vc
{
    class DX11Device;
    class DX11GBuffer;
    class DX11PipelineState;
    class DX11RenderTargets;

    struct DX11SSAOPassParameters
    {
        std::array<float, 2> inverseResolution { 1.0f / 1600.0f, 1.0f / 900.0f };
        float sampleRadius = 6.0f;
        float worldRadius = 0.60f;
        float intensity = 1.15f;
        float power = 1.20f;
        float normalBias = 0.08f;
        float rotation = 0.0f;
    };

    struct DX11SSAOPassInfo
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t passCount = 0;
        std::uint64_t clearCount = 0;
        std::uint64_t inputBindCount = 0;
        std::uint64_t parameterUpdateCount = 0;
        std::uint32_t sampleCount = 0;
        std::string format = "Unknown";
        bool ready = false;
    };

    class DX11SSAOPass
    {
    public:
        DX11SSAOPass(DX11Device& device, DX11RenderTargets& renderTargets, DX11GBuffer& gBuffer);
        ~DX11SSAOPass();

        DX11SSAOPass(const DX11SSAOPass&) = delete;
        DX11SSAOPass& operator=(const DX11SSAOPass&) = delete;
        DX11SSAOPass(DX11SSAOPass&&) noexcept;
        DX11SSAOPass& operator=(DX11SSAOPass&&) noexcept;

        [[nodiscard]] const DX11SSAOPassInfo& GetInfo() const noexcept;
        [[nodiscard]] const DX11SSAOPassParameters& GetParameters() const noexcept;
        [[nodiscard]] ID3D11Texture2D* GetOcclusionTexture() const noexcept;
        [[nodiscard]] ID3D11RenderTargetView* GetOcclusionRenderTargetView() const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* GetOcclusionShaderResourceView() const noexcept;

        void Resize(std::uint32_t width, std::uint32_t height);
        void Clear(float occlusion = 1.0f);
        void UpdateParameters(DX11SSAOPassParameters parameters);
        void Begin(DX11PipelineState& pipelineState);
        void End();

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
