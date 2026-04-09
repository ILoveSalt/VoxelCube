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
    class DX11RenderTargets;

    struct DX11GBufferInfo
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t renderTargetCount = 0;
        std::uint64_t bindCount = 0;
        std::uint64_t clearCount = 0;
        std::uint64_t previewCopyCount = 0;
        std::string albedoFormat = "Unknown";
        std::string normalFormat = "Unknown";
        std::string materialFormat = "Unknown";
        bool ready = false;
    };

    class DX11GBuffer
    {
    public:
        DX11GBuffer(DX11Device& device, DX11RenderTargets& renderTargets);
        ~DX11GBuffer();

        DX11GBuffer(const DX11GBuffer&) = delete;
        DX11GBuffer& operator=(const DX11GBuffer&) = delete;
        DX11GBuffer(DX11GBuffer&&) noexcept;
        DX11GBuffer& operator=(DX11GBuffer&&) noexcept;

        [[nodiscard]] const DX11GBufferInfo& GetInfo() const noexcept;
        [[nodiscard]] ID3D11Texture2D* GetAlbedoTexture() const noexcept;
        [[nodiscard]] ID3D11Texture2D* GetNormalTexture() const noexcept;
        [[nodiscard]] ID3D11Texture2D* GetMaterialTexture() const noexcept;
        [[nodiscard]] ID3D11RenderTargetView* GetAlbedoRenderTargetView() const noexcept;
        [[nodiscard]] ID3D11RenderTargetView* GetNormalRenderTargetView() const noexcept;
        [[nodiscard]] ID3D11RenderTargetView* GetMaterialRenderTargetView() const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* GetAlbedoShaderResourceView() const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* GetNormalShaderResourceView() const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* GetMaterialShaderResourceView() const noexcept;

        void Resize(std::uint32_t width, std::uint32_t height);
        void Bind();
        void Clear(
            const std::array<float, 4>& albedo,
            const std::array<float, 4>& normal,
            const std::array<float, 4>& material);
        void CopyAlbedoToBackBuffer();

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
