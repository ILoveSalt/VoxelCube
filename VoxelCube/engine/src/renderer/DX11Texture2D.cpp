#include <VoxelCube/Renderer/DX11Texture2D.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <VoxelCube/Core/FileSystem.hpp>
#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Device.hpp>

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr std::uint32_t kDdsMagic = 0x20534444u;
    constexpr std::uint32_t kDdsPixelFormatSize = 32u;
    constexpr std::uint32_t kDdsHeaderSize = 124u;
    constexpr std::uint32_t kDdsFourCcDx10 = 0x30315844u;
    constexpr std::uint32_t kDdpfRgb = 0x00000040u;
    constexpr std::uint32_t kDdpfAlphaPixels = 0x00000001u;

#pragma pack(push, 1)
    struct DdsPixelFormat
    {
        std::uint32_t size = 0;
        std::uint32_t flags = 0;
        std::uint32_t fourCC = 0;
        std::uint32_t rgbBitCount = 0;
        std::uint32_t rBitMask = 0;
        std::uint32_t gBitMask = 0;
        std::uint32_t bBitMask = 0;
        std::uint32_t aBitMask = 0;
    };

    struct DdsHeader
    {
        std::uint32_t size = 0;
        std::uint32_t flags = 0;
        std::uint32_t height = 0;
        std::uint32_t width = 0;
        std::uint32_t pitchOrLinearSize = 0;
        std::uint32_t depth = 0;
        std::uint32_t mipMapCount = 0;
        std::uint32_t reserved1[11] {};
        DdsPixelFormat pixelFormat {};
        std::uint32_t caps = 0;
        std::uint32_t caps2 = 0;
        std::uint32_t caps3 = 0;
        std::uint32_t caps4 = 0;
        std::uint32_t reserved2 = 0;
    };
#pragma pack(pop)

    static_assert(sizeof(DdsPixelFormat) == kDdsPixelFormatSize, "Unexpected DDS pixel-format size.");
    static_assert(sizeof(DdsHeader) == kDdsHeaderSize, "Unexpected DDS header size.");

    struct ParsedDdsImage
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t mipLevels = 0;
        std::uint64_t pixelDataSizeInBytes = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        std::vector<std::uint8_t> bytes;
        std::vector<D3D11_SUBRESOURCE_DATA> subresources;
    };

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
            case DXGI_FORMAT_R8G8B8A8_UNORM:
                return "R8G8B8A8_UNORM";
            case DXGI_FORMAT_B8G8R8A8_UNORM:
                return "B8G8R8A8_UNORM";
            default:
                return "Unknown";
        }
    }

    DXGI_FORMAT ResolveLegacyFormat(const DdsPixelFormat& pixelFormat)
    {
        const bool hasRgb = (pixelFormat.flags & kDdpfRgb) != 0;
        const bool hasAlpha = (pixelFormat.flags & kDdpfAlphaPixels) != 0;
        if (!hasRgb || !hasAlpha || pixelFormat.rgbBitCount != 32u)
        {
            return DXGI_FORMAT_UNKNOWN;
        }

        if (pixelFormat.rBitMask == 0x000000FFu
            && pixelFormat.gBitMask == 0x0000FF00u
            && pixelFormat.bBitMask == 0x00FF0000u
            && pixelFormat.aBitMask == 0xFF000000u)
        {
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        if (pixelFormat.rBitMask == 0x00FF0000u
            && pixelFormat.gBitMask == 0x0000FF00u
            && pixelFormat.bBitMask == 0x000000FFu
            && pixelFormat.aBitMask == 0xFF000000u)
        {
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        }

        return DXGI_FORMAT_UNKNOWN;
    }

    ParsedDdsImage ParseDdsFile(const vc::Path& path)
    {
        ParsedDdsImage image {};
        image.bytes = vc::FileSystem::ReadBytes(path);

        VC_ASSERT(image.bytes.size() >= sizeof(std::uint32_t) + sizeof(DdsHeader), "DDS file is too small.");

        std::uint32_t magic = 0;
        std::memcpy(&magic, image.bytes.data(), sizeof(magic));
        VC_ASSERT(magic == kDdsMagic, "DDS file is missing the DDS magic.");

        DdsHeader header {};
        std::memcpy(&header, image.bytes.data() + sizeof(magic), sizeof(header));
        VC_ASSERT(header.size == kDdsHeaderSize, "DDS file header has an invalid size.");
        VC_ASSERT(header.pixelFormat.size == kDdsPixelFormatSize, "DDS file pixel format has an invalid size.");
        VC_ASSERT(header.width > 0 && header.height > 0, "DDS file must describe a non-zero 2D texture.");
        VC_ASSERT(header.depth <= 1, "Only 2D DDS textures are supported right now.");
        VC_ASSERT(header.caps2 == 0, "Volume textures and cubemaps are not supported right now.");
        VC_ASSERT(header.pixelFormat.fourCC != kDdsFourCcDx10, "DDS DX10 extended headers are not supported right now.");

        image.format = ResolveLegacyFormat(header.pixelFormat);
        VC_ASSERT(image.format != DXGI_FORMAT_UNKNOWN, "DDS format is unsupported. Expected uncompressed 32-bit RGBA/BGRA.");

        image.width = header.width;
        image.height = header.height;
        image.mipLevels = std::max(1u, header.mipMapCount);

        constexpr std::uint32_t bytesPerPixel = 4u;
        std::size_t byteOffset = sizeof(magic) + sizeof(header);
        std::uint32_t mipWidth = image.width;
        std::uint32_t mipHeight = image.height;

        image.subresources.reserve(image.mipLevels);
        for (std::uint32_t mipIndex = 0; mipIndex < image.mipLevels; ++mipIndex)
        {
            const std::uint32_t rowPitch = mipWidth * bytesPerPixel;
            const std::uint32_t slicePitch = rowPitch * mipHeight;
            VC_ASSERT(
                byteOffset + slicePitch <= image.bytes.size(),
                "DDS file ended before all mip levels could be read.");

            D3D11_SUBRESOURCE_DATA subresource {};
            subresource.pSysMem = image.bytes.data() + byteOffset;
            subresource.SysMemPitch = rowPitch;
            subresource.SysMemSlicePitch = slicePitch;
            image.subresources.push_back(subresource);

            image.pixelDataSizeInBytes += slicePitch;
            byteOffset += slicePitch;
            mipWidth = std::max(1u, mipWidth / 2u);
            mipHeight = std::max(1u, mipHeight / 2u);
        }

        return image;
    }
}

namespace vc
{
    class DX11Texture2D::Impl
    {
    public:
        Impl(DX11Device& device, DX11Texture2DSpecification specification)
            : m_device(device)
            , m_specification(std::move(specification))
        {
            ValidateSpecification();
            CreateTextureResources();
        }

        [[nodiscard]] const DX11Texture2DSpecification& GetSpecification() const noexcept
        {
            return m_specification;
        }

        [[nodiscard]] const DX11Texture2DInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] ID3D11Texture2D* GetNativeTexture() const noexcept
        {
            return m_texture.Get();
        }

        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceView() const noexcept
        {
            return m_shaderResourceView.Get();
        }

        [[nodiscard]] ID3D11SamplerState* GetSamplerState() const noexcept
        {
            return m_samplerState.Get();
        }

        void BindPS(std::uint32_t slot)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 texture binding requires an immediate context.");

            ID3D11ShaderResourceView* shaderResourceView = m_shaderResourceView.Get();
            ID3D11SamplerState* samplerState = m_samplerState.Get();
            immediateContext->PSSetShaderResources(slot, 1, &shaderResourceView);
            immediateContext->PSSetSamplers(slot, 1, &samplerState);
            ++m_info.bindCount;
        }

    private:
        void ValidateSpecification() const
        {
            VC_ASSERT(!m_specification.path.empty(), "DX11 texture requires a source path.");
            VC_ASSERT(FileSystem::IsFile(m_specification.path), "DX11 texture source path must point to an existing file.");
        }

        void CreateTextureResources()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 texture creation requires a ready D3D11 device.");

            ParsedDdsImage parsedImage = ParseDdsFile(m_specification.path);

            D3D11_TEXTURE2D_DESC textureDescription {};
            textureDescription.Width = parsedImage.width;
            textureDescription.Height = parsedImage.height;
            textureDescription.MipLevels = parsedImage.mipLevels;
            textureDescription.ArraySize = 1;
            textureDescription.Format = parsedImage.format;
            textureDescription.SampleDesc.Count = 1;
            textureDescription.SampleDesc.Quality = 0;
            textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
            textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            textureDescription.CPUAccessFlags = 0;
            textureDescription.MiscFlags = 0;

            HRESULT result = nativeDevice->CreateTexture2D(
                &textureDescription,
                parsedImage.subresources.data(),
                &m_texture);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 texture from DDS: " + FormatHRESULT(result)).c_str());

            D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDescription {};
            shaderResourceViewDescription.Format = textureDescription.Format;
            shaderResourceViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            shaderResourceViewDescription.Texture2D.MostDetailedMip = 0;
            shaderResourceViewDescription.Texture2D.MipLevels = textureDescription.MipLevels;

            result = nativeDevice->CreateShaderResourceView(
                m_texture.Get(),
                &shaderResourceViewDescription,
                &m_shaderResourceView);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 texture shader-resource view: " + FormatHRESULT(result)).c_str());

            D3D11_SAMPLER_DESC samplerDescription {};
            samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDescription.MipLODBias = 0.0f;
            samplerDescription.MaxAnisotropy = 1;
            samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
            samplerDescription.BorderColor[0] = 0.0f;
            samplerDescription.BorderColor[1] = 0.0f;
            samplerDescription.BorderColor[2] = 0.0f;
            samplerDescription.BorderColor[3] = 0.0f;
            samplerDescription.MinLOD = 0.0f;
            samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;

            result = nativeDevice->CreateSamplerState(&samplerDescription, &m_samplerState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 texture sampler state: " + FormatHRESULT(result)).c_str());

            m_info.sourcePath = FileSystem::Normalize(m_specification.path);
            m_info.width = parsedImage.width;
            m_info.height = parsedImage.height;
            m_info.mipLevels = parsedImage.mipLevels;
            m_info.pixelDataSizeInBytes = parsedImage.pixelDataSizeInBytes;
            m_info.format = FormatToString(parsedImage.format);
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 texture created from DDS: " +
                m_info.sourcePath.string() +
                ", size=" +
                std::to_string(m_info.width) +
                "x" +
                std::to_string(m_info.height) +
                ", mips=" +
                std::to_string(m_info.mipLevels) +
                ", format=" +
                m_info.format +
                ", bytes=" +
                std::to_string(m_info.pixelDataSizeInBytes));
        }

    private:
        DX11Device& m_device;
        DX11Texture2DSpecification m_specification;
        DX11Texture2DInfo m_info;
        ComPtr<ID3D11Texture2D> m_texture;
        ComPtr<ID3D11ShaderResourceView> m_shaderResourceView;
        ComPtr<ID3D11SamplerState> m_samplerState;
    };

    DX11Texture2D::DX11Texture2D(DX11Device& device, DX11Texture2DSpecification specification)
        : m_impl(CreateScope<Impl>(device, std::move(specification)))
    {
    }

    DX11Texture2D::~DX11Texture2D() = default;

    DX11Texture2D::DX11Texture2D(DX11Texture2D&& other) noexcept = default;

    DX11Texture2D& DX11Texture2D::operator=(DX11Texture2D&& other) noexcept = default;

    const DX11Texture2DSpecification& DX11Texture2D::GetSpecification() const noexcept
    {
        return m_impl->GetSpecification();
    }

    const DX11Texture2DInfo& DX11Texture2D::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    ID3D11Texture2D* DX11Texture2D::GetNativeTexture() const noexcept
    {
        return m_impl->GetNativeTexture();
    }

    ID3D11ShaderResourceView* DX11Texture2D::GetShaderResourceView() const noexcept
    {
        return m_impl->GetShaderResourceView();
    }

    ID3D11SamplerState* DX11Texture2D::GetSamplerState() const noexcept
    {
        return m_impl->GetSamplerState();
    }

    void DX11Texture2D::BindPS(std::uint32_t slot)
    {
        m_impl->BindPS(slot);
    }
}
