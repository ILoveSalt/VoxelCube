#include <VoxelCube/Renderer/DX11SwapChain.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
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

    std::string FormatToString(DXGI_FORMAT format)
    {
        switch (format)
        {
            case DXGI_FORMAT_B8G8R8A8_UNORM:
                return "B8G8R8A8_UNORM";
            default:
                return "Unknown";
        }
    }

    std::string SwapEffectToString(DXGI_SWAP_EFFECT swapEffect)
    {
        switch (swapEffect)
        {
            case DXGI_SWAP_EFFECT_FLIP_DISCARD:
                return "FLIP_DISCARD";
            case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
                return "FLIP_SEQUENTIAL";
            case DXGI_SWAP_EFFECT_DISCARD:
                return "DISCARD";
            case DXGI_SWAP_EFFECT_SEQUENTIAL:
                return "SEQUENTIAL";
            default:
                return "Unknown";
        }
    }
}

namespace vc
{
    class DX11SwapChain::Impl
    {
    public:
        Impl(DX11Device& device, DX11SwapChainSpecification specification)
            : m_device(device)
            , m_specification(std::move(specification))
        {
            Initialize();
        }

        [[nodiscard]] const DX11SwapChainSpecification& GetSpecification() const noexcept
        {
            return m_specification;
        }

        [[nodiscard]] const DX11SwapChainInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] IDXGISwapChain1* GetNativeSwapChain() const noexcept
        {
            return m_swapChain.Get();
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

            const HRESULT result = m_swapChain->ResizeBuffers(
                m_info.bufferCount,
                width,
                height,
                DXGI_FORMAT_B8G8R8A8_UNORM,
                0);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to resize DXGI swap chain buffers: " + FormatHRESULT(result)).c_str());

            m_info.width = width;
            m_info.height = height;
            m_info.occluded = false;

            VC_LOG_INFO(
                "DX11 swap chain resized: " +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height));
        }

        [[nodiscard]] bool Present()
        {
            const UINT syncInterval = m_specification.enableVSync ? 1u : 0u;
            const HRESULT result = m_swapChain->Present(syncInterval, 0);
            if (result == DXGI_STATUS_OCCLUDED)
            {
                if (!m_info.occluded)
                {
                    VC_LOG_WARN("DX11 swap chain present is currently occluded.");
                }

                m_info.occluded = true;
                return false;
            }

            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to present DXGI swap chain: " + FormatHRESULT(result)).c_str());

            if (m_info.occluded)
            {
                VC_LOG_INFO("DX11 swap chain resumed presenting after occlusion.");
            }

            m_info.occluded = false;
            ++m_info.presentCount;
            return true;
        }

    private:
        void Initialize()
        {
            VC_ASSERT(m_specification.windowHandle != nullptr, "DX11 swap chain requires a valid window handle.");
            VC_ASSERT(m_specification.width > 0, "DX11 swap chain width must be greater than zero.");
            VC_ASSERT(m_specification.height > 0, "DX11 swap chain height must be greater than zero.");

            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 swap chain requires a ready D3D11 device.");

            HWND windowHandle = static_cast<HWND>(m_specification.windowHandle);

            ComPtr<IDXGIDevice> dxgiDevice;
            HRESULT result = nativeDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
            VC_ASSERT(SUCCEEDED(result), "Failed to query IDXGIDevice from D3D11 device.");

            ComPtr<IDXGIAdapter> adapter;
            result = dxgiDevice->GetAdapter(&adapter);
            VC_ASSERT(SUCCEEDED(result), "Failed to query DXGI adapter from DXGI device.");

            result = adapter->GetParent(IID_PPV_ARGS(&m_factory));
            VC_ASSERT(SUCCEEDED(result), "Failed to query IDXGIFactory2 from DXGI adapter.");

            DXGI_SWAP_CHAIN_DESC1 description {};
            description.Width = m_specification.width;
            description.Height = m_specification.height;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.SampleDesc.Quality = 0;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = std::max(2u, m_specification.bufferCount);
            description.Scaling = DXGI_SCALING_STRETCH;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            description.Flags = 0;

            result = m_factory->CreateSwapChainForHwnd(
                nativeDevice,
                windowHandle,
                &description,
                nullptr,
                nullptr,
                &m_swapChain);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DXGI swap chain: " + FormatHRESULT(result)).c_str());

            const HRESULT associationResult = m_factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);
            if (FAILED(associationResult))
            {
                VC_LOG_WARN(
                    "DXGI MakeWindowAssociation failed with " +
                    FormatHRESULT(associationResult) +
                    "; Alt+Enter fallback remains handled by Win32 window code.");
            }

            m_info.width = description.Width;
            m_info.height = description.Height;
            m_info.bufferCount = description.BufferCount;
            m_info.format = FormatToString(description.Format);
            m_info.swapEffect = SwapEffectToString(description.SwapEffect);
            m_info.vSyncEnabled = m_specification.enableVSync;

            VC_LOG_INFO(
                "DX11 swap chain created: " +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height) +
                ", buffers=" +
                std::to_string(m_info.bufferCount) +
                ", format=" +
                m_info.format +
                ", effect=" +
                m_info.swapEffect +
                ", vsync=" +
                std::string(m_info.vSyncEnabled ? "enabled" : "disabled"));
        }

    private:
        DX11Device& m_device;
        DX11SwapChainSpecification m_specification;
        DX11SwapChainInfo m_info;
        ComPtr<IDXGIFactory2> m_factory;
        ComPtr<IDXGISwapChain1> m_swapChain;
    };

    DX11SwapChain::DX11SwapChain(DX11Device& device, DX11SwapChainSpecification specification)
        : m_impl(CreateScope<Impl>(device, std::move(specification)))
    {
    }

    DX11SwapChain::~DX11SwapChain() = default;

    DX11SwapChain::DX11SwapChain(DX11SwapChain&& other) noexcept = default;

    DX11SwapChain& DX11SwapChain::operator=(DX11SwapChain&& other) noexcept = default;

    const DX11SwapChainSpecification& DX11SwapChain::GetSpecification() const noexcept
    {
        return m_impl->GetSpecification();
    }

    const DX11SwapChainInfo& DX11SwapChain::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    IDXGISwapChain1* DX11SwapChain::GetNativeSwapChain() const noexcept
    {
        return m_impl->GetNativeSwapChain();
    }

    void DX11SwapChain::Resize(std::uint32_t width, std::uint32_t height)
    {
        m_impl->Resize(width, height);
    }

    bool DX11SwapChain::Present()
    {
        return m_impl->Present();
    }
}
