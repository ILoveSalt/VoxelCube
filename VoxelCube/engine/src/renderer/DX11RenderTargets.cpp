#include <VoxelCube/Renderer/DX11RenderTargets.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Device.hpp>
#include <VoxelCube/Renderer/DX11SwapChain.hpp>

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
            case DXGI_FORMAT_R24G8_TYPELESS:
                return "R24G8_TYPELESS";
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
                return "D24_UNORM_S8_UINT";
            case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
                return "R24_UNORM_X8_TYPELESS";
            default:
                return "Unknown";
        }
    }
}

namespace vc
{
    class DX11RenderTargets::Impl
    {
    public:
        Impl(DX11Device& device, DX11SwapChain& swapChain)
            : m_device(device)
            , m_swapChain(swapChain)
        {
            const DX11SwapChainInfo& swapChainInfo = m_swapChain.GetInfo();
            Recreate(swapChainInfo.width, swapChainInfo.height);
        }

        [[nodiscard]] const DX11RenderTargetsInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] ID3D11RenderTargetView* GetRenderTargetView() const noexcept
        {
            return m_renderTargetView.Get();
        }

        [[nodiscard]] ID3D11Texture2D* GetBackBufferTexture() const noexcept
        {
            return m_backBuffer.Get();
        }

        [[nodiscard]] ID3D11DepthStencilView* GetDepthStencilView() const noexcept
        {
            return m_depthStencilView.Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* GetDepthShaderResourceView() const noexcept
        {
            return m_depthShaderResourceView.Get();
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

            ReleaseViewsForResize();
            m_swapChain.Resize(width, height);
            Recreate(width, height);
            VC_LOG_INFO(
                "DX11 render targets resized: " +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height));
        }

        void Bind()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 render targets require an immediate context.");

            ID3D11RenderTargetView* renderTargetView = m_renderTargetView.Get();
            immediateContext->OMSetRenderTargets(1, &renderTargetView, m_depthStencilView.Get());
        }

        void BindColorOnly()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 render targets require an immediate context.");

            ID3D11RenderTargetView* renderTargetView = m_renderTargetView.Get();
            immediateContext->OMSetRenderTargets(1, &renderTargetView, nullptr);
        }

        void BindDepthOnly()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 render targets require an immediate context.");

            immediateContext->OMSetRenderTargets(0, nullptr, m_depthStencilView.Get());
        }

        void Clear(const std::array<float, 4>& color, float depth, std::uint8_t stencil)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 render targets require an immediate context.");

            immediateContext->ClearRenderTargetView(m_renderTargetView.Get(), color.data());
            immediateContext->ClearDepthStencilView(
                m_depthStencilView.Get(),
                D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                depth,
                stencil);
            ++m_info.clearCount;
        }

    private:
        void ReleaseViewsForResize()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 render targets require an immediate context.");

            ID3D11RenderTargetView* nullRenderTargetView = nullptr;
            immediateContext->OMSetRenderTargets(1, &nullRenderTargetView, nullptr);

            m_renderTargetView.Reset();
            m_depthStencilView.Reset();
            m_depthShaderResourceView.Reset();
            m_depthTexture.Reset();
            m_backBuffer.Reset();
            m_info.ready = false;
        }

        void Recreate(std::uint32_t width, std::uint32_t height)
        {
            VC_ASSERT(width > 0, "DX11 render targets width must be greater than zero.");
            VC_ASSERT(height > 0, "DX11 render targets height must be greater than zero.");

            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 render targets require a ready D3D11 device.");

            IDXGISwapChain1* nativeSwapChain = m_swapChain.GetNativeSwapChain();
            VC_ASSERT(nativeSwapChain != nullptr, "DX11 render targets require a ready swap chain.");

            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 render targets require an immediate context.");

            HRESULT result = nativeSwapChain->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer));
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to fetch DXGI swap chain back buffer: " + FormatHRESULT(result)).c_str());

            result = nativeDevice->CreateRenderTargetView(m_backBuffer.Get(), nullptr, &m_renderTargetView);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create render target view: " + FormatHRESULT(result)).c_str());

            D3D11_TEXTURE2D_DESC depthTextureDescription {};
            depthTextureDescription.Width = width;
            depthTextureDescription.Height = height;
            depthTextureDescription.MipLevels = 1;
            depthTextureDescription.ArraySize = 1;
            depthTextureDescription.Format = DXGI_FORMAT_R24G8_TYPELESS;
            depthTextureDescription.SampleDesc.Count = 1;
            depthTextureDescription.SampleDesc.Quality = 0;
            depthTextureDescription.Usage = D3D11_USAGE_DEFAULT;
            depthTextureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            depthTextureDescription.CPUAccessFlags = 0;
            depthTextureDescription.MiscFlags = 0;

            result = nativeDevice->CreateTexture2D(&depthTextureDescription, nullptr, &m_depthTexture);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create depth-stencil texture: " + FormatHRESULT(result)).c_str());

            D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDescription {};
            depthStencilViewDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthStencilViewDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            depthStencilViewDescription.Flags = 0;
            depthStencilViewDescription.Texture2D.MipSlice = 0;

            result = nativeDevice->CreateDepthStencilView(
                m_depthTexture.Get(),
                &depthStencilViewDescription,
                &m_depthStencilView);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create depth-stencil view: " + FormatHRESULT(result)).c_str());

            D3D11_SHADER_RESOURCE_VIEW_DESC depthShaderResourceViewDescription {};
            depthShaderResourceViewDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            depthShaderResourceViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            depthShaderResourceViewDescription.Texture2D.MostDetailedMip = 0;
            depthShaderResourceViewDescription.Texture2D.MipLevels = 1;

            result = nativeDevice->CreateShaderResourceView(
                m_depthTexture.Get(),
                &depthShaderResourceViewDescription,
                &m_depthShaderResourceView);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create depth shader resource view: " + FormatHRESULT(result)).c_str());

            m_info.width = width;
            m_info.height = height;
            m_info.colorFormat = FormatToString(DXGI_FORMAT_B8G8R8A8_UNORM);
            m_info.depthResourceFormat = FormatToString(depthTextureDescription.Format);
            m_info.depthStencilFormat = FormatToString(depthStencilViewDescription.Format);
            m_info.depthShaderResourceFormat = FormatToString(depthShaderResourceViewDescription.Format);
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 render targets created: color=" +
                m_info.colorFormat +
                ", depth=" +
                m_info.depthStencilFormat +
                ", depthSRV=" +
                m_info.depthShaderResourceFormat +
                ", size=" +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height));
        }

    private:
        DX11Device& m_device;
        DX11SwapChain& m_swapChain;
        DX11RenderTargetsInfo m_info;
        ComPtr<ID3D11Texture2D> m_backBuffer;
        ComPtr<ID3D11Texture2D> m_depthTexture;
        ComPtr<ID3D11RenderTargetView> m_renderTargetView;
        ComPtr<ID3D11DepthStencilView> m_depthStencilView;
        ComPtr<ID3D11ShaderResourceView> m_depthShaderResourceView;
    };

    DX11RenderTargets::DX11RenderTargets(DX11Device& device, DX11SwapChain& swapChain)
        : m_impl(CreateScope<Impl>(device, swapChain))
    {
    }

    DX11RenderTargets::~DX11RenderTargets() = default;

    DX11RenderTargets::DX11RenderTargets(DX11RenderTargets&& other) noexcept = default;

    DX11RenderTargets& DX11RenderTargets::operator=(DX11RenderTargets&& other) noexcept = default;

    const DX11RenderTargetsInfo& DX11RenderTargets::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    ID3D11Texture2D* DX11RenderTargets::GetBackBufferTexture() const noexcept
    {
        return m_impl->GetBackBufferTexture();
    }

    ID3D11RenderTargetView* DX11RenderTargets::GetRenderTargetView() const noexcept
    {
        return m_impl->GetRenderTargetView();
    }

    ID3D11DepthStencilView* DX11RenderTargets::GetDepthStencilView() const noexcept
    {
        return m_impl->GetDepthStencilView();
    }

    ID3D11ShaderResourceView* DX11RenderTargets::GetDepthShaderResourceView() const noexcept
    {
        return m_impl->GetDepthShaderResourceView();
    }

    void DX11RenderTargets::Resize(std::uint32_t width, std::uint32_t height)
    {
        m_impl->Resize(width, height);
    }

    void DX11RenderTargets::Bind()
    {
        m_impl->Bind();
    }

    void DX11RenderTargets::BindColorOnly()
    {
        m_impl->BindColorOnly();
    }

    void DX11RenderTargets::BindDepthOnly()
    {
        m_impl->BindDepthOnly();
    }

    void DX11RenderTargets::Clear(const std::array<float, 4>& color, float depth, std::uint8_t stencil)
    {
        m_impl->Clear(color, depth, stencil);
    }
}
