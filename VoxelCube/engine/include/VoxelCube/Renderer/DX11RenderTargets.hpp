#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11ShaderResourceView;

namespace vc
{
    class DX11Device;
    class DX11SwapChain;

    struct DX11RenderTargetsInfo
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t clearCount = 0;
        std::string colorFormat = "Unknown";
        std::string depthResourceFormat = "Unknown";
        std::string depthStencilFormat = "Unknown";
        std::string depthShaderResourceFormat = "Unknown";
        bool ready = false;
    };

    class DX11RenderTargets
    {
    public:
        DX11RenderTargets(DX11Device& device, DX11SwapChain& swapChain);
        ~DX11RenderTargets();

        DX11RenderTargets(const DX11RenderTargets&) = delete;
        DX11RenderTargets& operator=(const DX11RenderTargets&) = delete;
        DX11RenderTargets(DX11RenderTargets&&) noexcept;
        DX11RenderTargets& operator=(DX11RenderTargets&&) noexcept;

        [[nodiscard]] const DX11RenderTargetsInfo& GetInfo() const noexcept;
        [[nodiscard]] ID3D11RenderTargetView* GetRenderTargetView() const noexcept;
        [[nodiscard]] ID3D11DepthStencilView* GetDepthStencilView() const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* GetDepthShaderResourceView() const noexcept;

        void Resize(std::uint32_t width, std::uint32_t height);
        void Bind();
        void Clear(
            const std::array<float, 4>& color,
            float depth = 1.0f,
            std::uint8_t stencil = 0);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
