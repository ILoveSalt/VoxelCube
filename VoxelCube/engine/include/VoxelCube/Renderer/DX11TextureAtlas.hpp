#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    class DX11Texture2D;

    struct DX11TextureAtlasSpecification
    {
        bool buildUniformGrid = false;
        std::uint32_t tileWidth = 0;
        std::uint32_t tileHeight = 0;
        std::uint32_t paddingX = 0;
        std::uint32_t paddingY = 0;
        std::uint32_t marginX = 0;
        std::uint32_t marginY = 0;
        std::string regionNamePrefix = "tile";
    };

    struct DX11TextureAtlasRegion
    {
        std::string name;
        std::uint32_t pixelX = 0;
        std::uint32_t pixelY = 0;
        std::uint32_t pixelWidth = 0;
        std::uint32_t pixelHeight = 0;
        float uMin = 0.0f;
        float vMin = 0.0f;
        float uMax = 0.0f;
        float vMax = 0.0f;
        float uCenter = 0.0f;
        float vCenter = 0.0f;
    };

    struct DX11TextureAtlasInfo
    {
        Path texturePath;
        std::uint32_t textureWidth = 0;
        std::uint32_t textureHeight = 0;
        std::uint32_t regionCount = 0;
        std::uint64_t bindCount = 0;
        bool ready = false;
    };

    class DX11TextureAtlas
    {
    public:
        DX11TextureAtlas(DX11Texture2D& texture, DX11TextureAtlasSpecification specification = {});
        ~DX11TextureAtlas();

        DX11TextureAtlas(const DX11TextureAtlas&) = delete;
        DX11TextureAtlas& operator=(const DX11TextureAtlas&) = delete;
        DX11TextureAtlas(DX11TextureAtlas&&) noexcept;
        DX11TextureAtlas& operator=(DX11TextureAtlas&&) noexcept;

        [[nodiscard]] const DX11TextureAtlasSpecification& GetSpecification() const noexcept;
        [[nodiscard]] const DX11TextureAtlasInfo& GetInfo() const noexcept;
        [[nodiscard]] const DX11Texture2D& GetTexture() const noexcept;
        [[nodiscard]] const std::vector<DX11TextureAtlasRegion>& GetRegions() const noexcept;
        [[nodiscard]] bool HasRegion(std::string_view name) const noexcept;
        [[nodiscard]] const DX11TextureAtlasRegion* FindRegion(std::string_view name) const noexcept;
        [[nodiscard]] const DX11TextureAtlasRegion& GetRegion(std::string_view name) const;
        [[nodiscard]] const DX11TextureAtlasRegion& GetRegion(std::size_t index) const;

        void AddRegion(std::string name, std::uint32_t pixelX, std::uint32_t pixelY, std::uint32_t pixelWidth, std::uint32_t pixelHeight);
        void BuildUniformGrid();
        void BindPS(std::uint32_t slot = 0);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
