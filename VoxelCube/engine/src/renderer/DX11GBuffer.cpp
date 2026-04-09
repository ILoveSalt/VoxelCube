#include <VoxelCube/Renderer/DX11GBuffer.hpp>

#include <array>
#include <iomanip>
#include <sstream>
#include <utility>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Device.hpp>
#include <VoxelCube/Renderer/DX11RenderTargets.hpp>

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr std::size_t kGBufferTargetCount = 3;
    constexpr std::size_t kAlbedoTargetIndex = 0;
    constexpr std::size_t kNormalTargetIndex = 1;
    constexpr std::size_t kMaterialTargetIndex = 2;

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
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                return "R16G16B16A16_FLOAT";
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                return "R8G8B8A8_UNORM";
            default:
                return "Unknown";
        }
    }
}

namespace vc
{
    class DX11GBuffer::Impl
    {
    public:
        Impl(DX11Device& device, DX11RenderTargets& renderTargets)
            : m_device(device)
            , m_renderTargets(renderTargets)
        {
            const DX11RenderTargetsInfo& renderTargetsInfo = m_renderTargets.GetInfo();
            Recreate(renderTargetsInfo.width, renderTargetsInfo.height);
        }

        [[nodiscard]] const DX11GBufferInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] ID3D11Texture2D* GetAlbedoTexture() const noexcept
        {
            return m_textures[kAlbedoTargetIndex].Get();
        }

        [[nodiscard]] ID3D11Texture2D* GetNormalTexture() const noexcept
        {
            return m_textures[kNormalTargetIndex].Get();
        }

        [[nodiscard]] ID3D11Texture2D* GetMaterialTexture() const noexcept
        {
            return m_textures[kMaterialTargetIndex].Get();
        }

        [[nodiscard]] ID3D11RenderTargetView* GetAlbedoRenderTargetView() const noexcept
        {
            return m_renderTargetViews[kAlbedoTargetIndex].Get();
        }

        [[nodiscard]] ID3D11RenderTargetView* GetNormalRenderTargetView() const noexcept
        {
            return m_renderTargetViews[kNormalTargetIndex].Get();
        }

        [[nodiscard]] ID3D11RenderTargetView* GetMaterialRenderTargetView() const noexcept
        {
            return m_renderTargetViews[kMaterialTargetIndex].Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* GetAlbedoShaderResourceView() const noexcept
        {
            return m_shaderResourceViews[kAlbedoTargetIndex].Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* GetNormalShaderResourceView() const noexcept
        {
            return m_shaderResourceViews[kNormalTargetIndex].Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* GetMaterialShaderResourceView() const noexcept
        {
            return m_shaderResourceViews[kMaterialTargetIndex].Get();
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
            VC_LOG_INFO(
                "DX11 G-buffer resized: " +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height));
        }

        void Bind()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 G-buffer requires an immediate context.");

            ID3D11DepthStencilView* depthStencilView = m_renderTargets.GetDepthStencilView();
            VC_ASSERT(depthStencilView != nullptr, "DX11 G-buffer requires a ready depth-stencil view.");

            std::array<ID3D11RenderTargetView*, kGBufferTargetCount> renderTargetViews {
                m_renderTargetViews[kAlbedoTargetIndex].Get(),
                m_renderTargetViews[kNormalTargetIndex].Get(),
                m_renderTargetViews[kMaterialTargetIndex].Get()
            };
            immediateContext->OMSetRenderTargets(
                static_cast<UINT>(renderTargetViews.size()),
                renderTargetViews.data(),
                depthStencilView);
            ++m_info.bindCount;
        }

        void Clear(
            const std::array<float, 4>& albedo,
            const std::array<float, 4>& normal,
            const std::array<float, 4>& material)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 G-buffer requires an immediate context.");

            immediateContext->ClearRenderTargetView(m_renderTargetViews[kAlbedoTargetIndex].Get(), albedo.data());
            immediateContext->ClearRenderTargetView(m_renderTargetViews[kNormalTargetIndex].Get(), normal.data());
            immediateContext->ClearRenderTargetView(m_renderTargetViews[kMaterialTargetIndex].Get(), material.data());
            ++m_info.clearCount;
        }

        void CopyAlbedoToBackBuffer()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 G-buffer requires an immediate context.");

            ID3D11Texture2D* backBufferTexture = m_renderTargets.GetBackBufferTexture();
            VC_ASSERT(backBufferTexture != nullptr, "DX11 G-buffer preview copy requires a ready back buffer texture.");

            immediateContext->OMSetRenderTargets(0, nullptr, nullptr);
            immediateContext->CopyResource(backBufferTexture, m_textures[kAlbedoTargetIndex].Get());
            ++m_info.previewCopyCount;
        }

    private:
        void ReleaseResources()
        {
            for (ComPtr<ID3D11ShaderResourceView>& shaderResourceView : m_shaderResourceViews)
            {
                shaderResourceView.Reset();
            }

            for (ComPtr<ID3D11RenderTargetView>& renderTargetView : m_renderTargetViews)
            {
                renderTargetView.Reset();
            }

            for (ComPtr<ID3D11Texture2D>& texture : m_textures)
            {
                texture.Reset();
            }

            m_info.ready = false;
        }

        void Recreate(std::uint32_t width, std::uint32_t height)
        {
            VC_ASSERT(width > 0, "DX11 G-buffer width must be greater than zero.");
            VC_ASSERT(height > 0, "DX11 G-buffer height must be greater than zero.");

            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 G-buffer requires a ready D3D11 device.");

            const std::array<DXGI_FORMAT, kGBufferTargetCount> formats {
                DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_FORMAT_R16G16B16A16_FLOAT,
                DXGI_FORMAT_R16G16B16A16_FLOAT
            };

            for (std::size_t index = 0; index < formats.size(); ++index)
            {
                D3D11_TEXTURE2D_DESC textureDescription {};
                textureDescription.Width = width;
                textureDescription.Height = height;
                textureDescription.MipLevels = 1;
                textureDescription.ArraySize = 1;
                textureDescription.Format = formats[index];
                textureDescription.SampleDesc.Count = 1;
                textureDescription.SampleDesc.Quality = 0;
                textureDescription.Usage = D3D11_USAGE_DEFAULT;
                textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                textureDescription.CPUAccessFlags = 0;
                textureDescription.MiscFlags = 0;

                HRESULT result = nativeDevice->CreateTexture2D(&textureDescription, nullptr, &m_textures[index]);
                VC_ASSERT(
                    SUCCEEDED(result),
                    ("Failed to create DX11 G-buffer texture: " + FormatHRESULT(result)).c_str());

                result = nativeDevice->CreateRenderTargetView(m_textures[index].Get(), nullptr, &m_renderTargetViews[index]);
                VC_ASSERT(
                    SUCCEEDED(result),
                    ("Failed to create DX11 G-buffer render-target view: " + FormatHRESULT(result)).c_str());

                result = nativeDevice->CreateShaderResourceView(m_textures[index].Get(), nullptr, &m_shaderResourceViews[index]);
                VC_ASSERT(
                    SUCCEEDED(result),
                    ("Failed to create DX11 G-buffer shader-resource view: " + FormatHRESULT(result)).c_str());
            }

            m_info.width = width;
            m_info.height = height;
            m_info.renderTargetCount = static_cast<std::uint32_t>(formats.size());
            m_info.albedoFormat = FormatToString(formats[kAlbedoTargetIndex]);
            m_info.normalFormat = FormatToString(formats[kNormalTargetIndex]);
            m_info.materialFormat = FormatToString(formats[kMaterialTargetIndex]);
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 G-buffer created: size=" +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height) +
                ", targets=" +
                std::to_string(m_info.renderTargetCount) +
                ", albedo=" +
                m_info.albedoFormat +
                ", normal=" +
                m_info.normalFormat +
                ", material=" +
                m_info.materialFormat);
        }

    private:
        DX11Device& m_device;
        DX11RenderTargets& m_renderTargets;
        DX11GBufferInfo m_info;
        std::array<ComPtr<ID3D11Texture2D>, kGBufferTargetCount> m_textures;
        std::array<ComPtr<ID3D11RenderTargetView>, kGBufferTargetCount> m_renderTargetViews;
        std::array<ComPtr<ID3D11ShaderResourceView>, kGBufferTargetCount> m_shaderResourceViews;
    };

    DX11GBuffer::DX11GBuffer(DX11Device& device, DX11RenderTargets& renderTargets)
        : m_impl(CreateScope<Impl>(device, renderTargets))
    {
    }

    DX11GBuffer::~DX11GBuffer() = default;

    DX11GBuffer::DX11GBuffer(DX11GBuffer&& other) noexcept = default;

    DX11GBuffer& DX11GBuffer::operator=(DX11GBuffer&& other) noexcept = default;

    const DX11GBufferInfo& DX11GBuffer::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    ID3D11Texture2D* DX11GBuffer::GetAlbedoTexture() const noexcept
    {
        return m_impl->GetAlbedoTexture();
    }

    ID3D11Texture2D* DX11GBuffer::GetNormalTexture() const noexcept
    {
        return m_impl->GetNormalTexture();
    }

    ID3D11Texture2D* DX11GBuffer::GetMaterialTexture() const noexcept
    {
        return m_impl->GetMaterialTexture();
    }

    ID3D11RenderTargetView* DX11GBuffer::GetAlbedoRenderTargetView() const noexcept
    {
        return m_impl->GetAlbedoRenderTargetView();
    }

    ID3D11RenderTargetView* DX11GBuffer::GetNormalRenderTargetView() const noexcept
    {
        return m_impl->GetNormalRenderTargetView();
    }

    ID3D11RenderTargetView* DX11GBuffer::GetMaterialRenderTargetView() const noexcept
    {
        return m_impl->GetMaterialRenderTargetView();
    }

    ID3D11ShaderResourceView* DX11GBuffer::GetAlbedoShaderResourceView() const noexcept
    {
        return m_impl->GetAlbedoShaderResourceView();
    }

    ID3D11ShaderResourceView* DX11GBuffer::GetNormalShaderResourceView() const noexcept
    {
        return m_impl->GetNormalShaderResourceView();
    }

    ID3D11ShaderResourceView* DX11GBuffer::GetMaterialShaderResourceView() const noexcept
    {
        return m_impl->GetMaterialShaderResourceView();
    }

    void DX11GBuffer::Resize(std::uint32_t width, std::uint32_t height)
    {
        m_impl->Resize(width, height);
    }

    void DX11GBuffer::Bind()
    {
        m_impl->Bind();
    }

    void DX11GBuffer::Clear(
        const std::array<float, 4>& albedo,
        const std::array<float, 4>& normal,
        const std::array<float, 4>& material)
    {
        m_impl->Clear(albedo, normal, material);
    }

    void DX11GBuffer::CopyAlbedoToBackBuffer()
    {
        m_impl->CopyAlbedoToBackBuffer();
    }
}
