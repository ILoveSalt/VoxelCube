#pragma once

#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

struct IDXGISwapChain1;

namespace vc
{
    class DX11Device;

    struct DX11SwapChainSpecification
    {
        void* windowHandle = nullptr;
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        std::uint32_t bufferCount = 2;
        bool enableVSync = true;
    };

    struct DX11SwapChainInfo
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t bufferCount = 0;
        std::uint64_t presentCount = 0;
        std::string format = "Unknown";
        std::string swapEffect = "Unknown";
        bool vSyncEnabled = true;
        bool occluded = false;
    };

    class DX11SwapChain
    {
    public:
        DX11SwapChain(DX11Device& device, DX11SwapChainSpecification specification = {});
        ~DX11SwapChain();

        DX11SwapChain(const DX11SwapChain&) = delete;
        DX11SwapChain& operator=(const DX11SwapChain&) = delete;
        DX11SwapChain(DX11SwapChain&&) noexcept;
        DX11SwapChain& operator=(DX11SwapChain&&) noexcept;

        [[nodiscard]] const DX11SwapChainSpecification& GetSpecification() const noexcept;
        [[nodiscard]] const DX11SwapChainInfo& GetInfo() const noexcept;
        [[nodiscard]] IDXGISwapChain1* GetNativeSwapChain() const noexcept;

        void Resize(std::uint32_t width, std::uint32_t height);
        [[nodiscard]] bool Present();

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
