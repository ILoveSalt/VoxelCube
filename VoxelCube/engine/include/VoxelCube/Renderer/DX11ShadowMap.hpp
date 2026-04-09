#pragma once

#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace vc
{
    class DX11Device;

    struct DX11ShadowMapSpecification
    {
        std::uint32_t width = 1024;
        std::uint32_t height = 1024;
        std::uint32_t cascadeCount = 2;
    };

    struct DX11ShadowMapInfo
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t cascadeCount = 0;
        std::uint64_t bindCount = 0;
        std::uint64_t clearCount = 0;
        std::uint64_t shadowPassCount = 0;
        std::string resourceFormat = "Unknown";
        std::string depthStencilFormat = "Unknown";
        std::string shaderResourceFormat = "Unknown";
        bool ready = false;
    };

    class DX11ShadowMap
    {
    public:
        DX11ShadowMap(DX11Device& device, DX11ShadowMapSpecification specification = {});
        ~DX11ShadowMap();

        DX11ShadowMap(const DX11ShadowMap&) = delete;
        DX11ShadowMap& operator=(const DX11ShadowMap&) = delete;
        DX11ShadowMap(DX11ShadowMap&&) noexcept;
        DX11ShadowMap& operator=(DX11ShadowMap&&) noexcept;

        [[nodiscard]] const DX11ShadowMapSpecification& GetSpecification() const noexcept;
        [[nodiscard]] const DX11ShadowMapInfo& GetInfo() const noexcept;
        [[nodiscard]] ID3D11Texture2D* GetNativeTexture() const noexcept;
        [[nodiscard]] ID3D11DepthStencilView* GetCascadeDepthStencilView(std::uint32_t cascadeIndex) const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* GetDepthShaderResourceView() const noexcept;

        void Clear(float depth = 1.0f);
        void BindCascade(std::uint32_t cascadeIndex);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
