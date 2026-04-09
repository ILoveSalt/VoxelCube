#include <VoxelCube/Renderer/DX11ShadowMap.hpp>

#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
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
}

namespace vc
{
    class DX11ShadowMap::Impl
    {
    public:
        Impl(DX11Device& device, DX11ShadowMapSpecification specification)
            : m_device(device)
            , m_specification(std::move(specification))
        {
            ValidateSpecification();
            Initialize();
        }

        [[nodiscard]] const DX11ShadowMapSpecification& GetSpecification() const noexcept
        {
            return m_specification;
        }

        [[nodiscard]] const DX11ShadowMapInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] ID3D11Texture2D* GetNativeTexture() const noexcept
        {
            return m_texture.Get();
        }

        [[nodiscard]] ID3D11DepthStencilView* GetCascadeDepthStencilView(std::uint32_t cascadeIndex) const noexcept
        {
            VC_ASSERT(cascadeIndex < m_depthStencilViews.size(), "DX11 shadow map cascade index is out of range.");
            return m_depthStencilViews[cascadeIndex].Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* GetDepthShaderResourceView() const noexcept
        {
            return m_shaderResourceView.Get();
        }

        void Clear(float depth)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 shadow map requires an immediate context.");

            for (const ComPtr<ID3D11DepthStencilView>& depthStencilView : m_depthStencilViews)
            {
                immediateContext->ClearDepthStencilView(depthStencilView.Get(), D3D11_CLEAR_DEPTH, depth, 0);
            }

            ++m_info.clearCount;
        }

        void BindCascade(std::uint32_t cascadeIndex)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 shadow map requires an immediate context.");
            VC_ASSERT(cascadeIndex < m_depthStencilViews.size(), "DX11 shadow map cascade index is out of range.");

            immediateContext->OMSetRenderTargets(0, nullptr, m_depthStencilViews[cascadeIndex].Get());

            D3D11_VIEWPORT viewport {};
            viewport.TopLeftX = 0.0f;
            viewport.TopLeftY = 0.0f;
            viewport.Width = static_cast<float>(m_specification.width);
            viewport.Height = static_cast<float>(m_specification.height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            immediateContext->RSSetViewports(1, &viewport);

            ++m_info.bindCount;
            ++m_info.shadowPassCount;
        }

    private:
        void ValidateSpecification() const
        {
            VC_ASSERT(m_specification.width > 0, "DX11 shadow map width must be greater than zero.");
            VC_ASSERT(m_specification.height > 0, "DX11 shadow map height must be greater than zero.");
            VC_ASSERT(m_specification.cascadeCount > 0, "DX11 shadow map requires at least one cascade.");
        }

        void Initialize()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 shadow map requires a ready D3D11 device.");

            D3D11_TEXTURE2D_DESC textureDescription {};
            textureDescription.Width = m_specification.width;
            textureDescription.Height = m_specification.height;
            textureDescription.MipLevels = 1;
            textureDescription.ArraySize = m_specification.cascadeCount;
            textureDescription.Format = DXGI_FORMAT_R32_TYPELESS;
            textureDescription.SampleDesc.Count = 1;
            textureDescription.SampleDesc.Quality = 0;
            textureDescription.Usage = D3D11_USAGE_DEFAULT;
            textureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            textureDescription.CPUAccessFlags = 0;
            textureDescription.MiscFlags = 0;

            HRESULT result = nativeDevice->CreateTexture2D(&textureDescription, nullptr, &m_texture);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 shadow-map texture: " + FormatHRESULT(result)).c_str());

            m_depthStencilViews.resize(m_specification.cascadeCount);
            for (std::uint32_t cascadeIndex = 0; cascadeIndex < m_specification.cascadeCount; ++cascadeIndex)
            {
                D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDescription {};
                depthStencilViewDescription.Format = DXGI_FORMAT_D32_FLOAT;
                depthStencilViewDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                depthStencilViewDescription.Flags = 0;
                depthStencilViewDescription.Texture2DArray.MipSlice = 0;
                depthStencilViewDescription.Texture2DArray.FirstArraySlice = cascadeIndex;
                depthStencilViewDescription.Texture2DArray.ArraySize = 1;

                result = nativeDevice->CreateDepthStencilView(
                    m_texture.Get(),
                    &depthStencilViewDescription,
                    &m_depthStencilViews[cascadeIndex]);
                VC_ASSERT(
                    SUCCEEDED(result),
                    ("Failed to create DX11 shadow-map DSV: " + FormatHRESULT(result)).c_str());
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDescription {};
            shaderResourceViewDescription.Format = DXGI_FORMAT_R32_FLOAT;
            shaderResourceViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            shaderResourceViewDescription.Texture2DArray.MostDetailedMip = 0;
            shaderResourceViewDescription.Texture2DArray.MipLevels = 1;
            shaderResourceViewDescription.Texture2DArray.FirstArraySlice = 0;
            shaderResourceViewDescription.Texture2DArray.ArraySize = m_specification.cascadeCount;

            result = nativeDevice->CreateShaderResourceView(
                m_texture.Get(),
                &shaderResourceViewDescription,
                &m_shaderResourceView);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 shadow-map SRV: " + FormatHRESULT(result)).c_str());

            m_info.width = m_specification.width;
            m_info.height = m_specification.height;
            m_info.cascadeCount = m_specification.cascadeCount;
            m_info.resourceFormat = "R32_TYPELESS";
            m_info.depthStencilFormat = "D32_FLOAT";
            m_info.shaderResourceFormat = "R32_FLOAT";
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 shadow map created: size=" +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height) +
                ", cascades=" +
                std::to_string(m_info.cascadeCount) +
                ", resource=" +
                m_info.resourceFormat +
                ", dsv=" +
                m_info.depthStencilFormat +
                ", srv=" +
                m_info.shaderResourceFormat);
        }

    private:
        DX11Device& m_device;
        DX11ShadowMapSpecification m_specification;
        DX11ShadowMapInfo m_info;
        ComPtr<ID3D11Texture2D> m_texture;
        std::vector<ComPtr<ID3D11DepthStencilView>> m_depthStencilViews;
        ComPtr<ID3D11ShaderResourceView> m_shaderResourceView;
    };

    DX11ShadowMap::DX11ShadowMap(DX11Device& device, DX11ShadowMapSpecification specification)
        : m_impl(CreateScope<Impl>(device, std::move(specification)))
    {
    }

    DX11ShadowMap::~DX11ShadowMap() = default;

    DX11ShadowMap::DX11ShadowMap(DX11ShadowMap&& other) noexcept = default;

    DX11ShadowMap& DX11ShadowMap::operator=(DX11ShadowMap&& other) noexcept = default;

    const DX11ShadowMapSpecification& DX11ShadowMap::GetSpecification() const noexcept
    {
        return m_impl->GetSpecification();
    }

    const DX11ShadowMapInfo& DX11ShadowMap::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    ID3D11Texture2D* DX11ShadowMap::GetNativeTexture() const noexcept
    {
        return m_impl->GetNativeTexture();
    }

    ID3D11DepthStencilView* DX11ShadowMap::GetCascadeDepthStencilView(std::uint32_t cascadeIndex) const noexcept
    {
        return m_impl->GetCascadeDepthStencilView(cascadeIndex);
    }

    ID3D11ShaderResourceView* DX11ShadowMap::GetDepthShaderResourceView() const noexcept
    {
        return m_impl->GetDepthShaderResourceView();
    }

    void DX11ShadowMap::Clear(float depth)
    {
        m_impl->Clear(depth);
    }

    void DX11ShadowMap::BindCascade(std::uint32_t cascadeIndex)
    {
        m_impl->BindCascade(cascadeIndex);
    }
}
