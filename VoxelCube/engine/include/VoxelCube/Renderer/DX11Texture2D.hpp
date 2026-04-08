#pragma once

#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace vc
{
    class DX11Device;

    struct DX11Texture2DSpecification
    {
        Path path;
    };

    struct DX11Texture2DInfo
    {
        Path sourcePath;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t mipLevels = 0;
        std::uint64_t bindCount = 0;
        std::uint64_t pixelDataSizeInBytes = 0;
        std::string format = "Unknown";
        bool ready = false;
    };

    class DX11Texture2D
    {
    public:
        DX11Texture2D(DX11Device& device, DX11Texture2DSpecification specification = {});
        ~DX11Texture2D();

        DX11Texture2D(const DX11Texture2D&) = delete;
        DX11Texture2D& operator=(const DX11Texture2D&) = delete;
        DX11Texture2D(DX11Texture2D&&) noexcept;
        DX11Texture2D& operator=(DX11Texture2D&&) noexcept;

        [[nodiscard]] const DX11Texture2DSpecification& GetSpecification() const noexcept;
        [[nodiscard]] const DX11Texture2DInfo& GetInfo() const noexcept;
        [[nodiscard]] ID3D11Texture2D* GetNativeTexture() const noexcept;
        [[nodiscard]] ID3D11ShaderResourceView* GetShaderResourceView() const noexcept;
        [[nodiscard]] ID3D11SamplerState* GetSamplerState() const noexcept;

        void BindPS(std::uint32_t slot = 0);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
