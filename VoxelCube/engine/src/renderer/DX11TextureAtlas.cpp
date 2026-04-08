#include <VoxelCube/Renderer/DX11TextureAtlas.hpp>

#include <algorithm>
#include <utility>

#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Texture2D.hpp>

namespace
{
    float ComputeRegionMinUv(std::uint32_t pixelOffset, std::uint32_t textureExtent)
    {
        VC_ASSERT(textureExtent > 0, "Texture extent must be greater than zero.");
        return (static_cast<float>(pixelOffset) + 0.5f) / static_cast<float>(textureExtent);
    }

    float ComputeRegionMaxUv(std::uint32_t pixelOffset, std::uint32_t pixelExtent, std::uint32_t textureExtent)
    {
        VC_ASSERT(pixelExtent > 0, "Texture region extent must be greater than zero.");
        VC_ASSERT(textureExtent > 0, "Texture extent must be greater than zero.");
        return (static_cast<float>(pixelOffset + pixelExtent) - 0.5f) / static_cast<float>(textureExtent);
    }
}

namespace vc
{
    class DX11TextureAtlas::Impl
    {
    public:
        Impl(DX11Texture2D& texture, DX11TextureAtlasSpecification specification)
            : m_texture(texture)
            , m_specification(std::move(specification))
        {
            const DX11Texture2DInfo& textureInfo = m_texture.GetInfo();
            VC_ASSERT(textureInfo.ready, "DX11 texture atlas requires a ready DX11Texture2D.");

            m_info.texturePath = textureInfo.sourcePath;
            m_info.textureWidth = textureInfo.width;
            m_info.textureHeight = textureInfo.height;

            if (m_specification.buildUniformGrid)
            {
                BuildUniformGrid();
            }

            m_info.ready = true;
            VC_LOG_INFO(
                "DX11 texture atlas initialized: texture=" +
                m_info.texturePath.string() +
                ", size=" +
                std::to_string(m_info.textureWidth) +
                "x" +
                std::to_string(m_info.textureHeight) +
                ", regions=" +
                std::to_string(m_info.regionCount));
        }

        [[nodiscard]] const DX11TextureAtlasSpecification& GetSpecification() const noexcept
        {
            return m_specification;
        }

        [[nodiscard]] const DX11TextureAtlasInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] const DX11Texture2D& GetTexture() const noexcept
        {
            return m_texture;
        }

        [[nodiscard]] const std::vector<DX11TextureAtlasRegion>& GetRegions() const noexcept
        {
            return m_regions;
        }

        [[nodiscard]] bool HasRegion(std::string_view name) const noexcept
        {
            return FindRegion(name) != nullptr;
        }

        [[nodiscard]] const DX11TextureAtlasRegion* FindRegion(std::string_view name) const noexcept
        {
            const auto iterator = std::find_if(
                m_regions.begin(),
                m_regions.end(),
                [name](const DX11TextureAtlasRegion& region)
                {
                    return region.name == name;
                });
            return iterator != m_regions.end() ? &(*iterator) : nullptr;
        }

        [[nodiscard]] const DX11TextureAtlasRegion& GetRegion(std::string_view name) const
        {
            const DX11TextureAtlasRegion* region = FindRegion(name);
            VC_ASSERT(region != nullptr, ("DX11 texture atlas region was not found: " + std::string(name)).c_str());
            return *region;
        }

        [[nodiscard]] const DX11TextureAtlasRegion& GetRegion(std::size_t index) const
        {
            VC_ASSERT(index < m_regions.size(), "DX11 texture atlas region index is out of range.");
            return m_regions[index];
        }

        void AddRegion(std::string name, std::uint32_t pixelX, std::uint32_t pixelY, std::uint32_t pixelWidth, std::uint32_t pixelHeight)
        {
            VC_ASSERT(!name.empty(), "DX11 texture atlas regions must have a name.");
            VC_ASSERT(pixelWidth > 0 && pixelHeight > 0, "DX11 texture atlas region size must be greater than zero.");
            VC_ASSERT(pixelX + pixelWidth <= m_info.textureWidth, "DX11 texture atlas region exceeds texture width.");
            VC_ASSERT(pixelY + pixelHeight <= m_info.textureHeight, "DX11 texture atlas region exceeds texture height.");
            VC_ASSERT(!HasRegion(name), ("DX11 texture atlas region already exists: " + name).c_str());

            DX11TextureAtlasRegion region {};
            region.name = std::move(name);
            region.pixelX = pixelX;
            region.pixelY = pixelY;
            region.pixelWidth = pixelWidth;
            region.pixelHeight = pixelHeight;
            region.uMin = ComputeRegionMinUv(region.pixelX, m_info.textureWidth);
            region.vMin = ComputeRegionMinUv(region.pixelY, m_info.textureHeight);
            region.uMax = ComputeRegionMaxUv(region.pixelX, region.pixelWidth, m_info.textureWidth);
            region.vMax = ComputeRegionMaxUv(region.pixelY, region.pixelHeight, m_info.textureHeight);
            region.uCenter = (region.uMin + region.uMax) * 0.5f;
            region.vCenter = (region.vMin + region.vMax) * 0.5f;

            m_regions.push_back(std::move(region));
            m_info.regionCount = static_cast<std::uint32_t>(m_regions.size());
        }

        void BuildUniformGrid()
        {
            VC_ASSERT(m_specification.tileWidth > 0, "DX11 texture atlas grid requires a non-zero tileWidth.");
            VC_ASSERT(m_specification.tileHeight > 0, "DX11 texture atlas grid requires a non-zero tileHeight.");
            VC_ASSERT(m_specification.marginX * 2u < m_info.textureWidth + 1u, "DX11 texture atlas marginX is too large.");
            VC_ASSERT(m_specification.marginY * 2u < m_info.textureHeight + 1u, "DX11 texture atlas marginY is too large.");

            std::uint32_t row = 0;
            for (std::uint32_t pixelY = m_specification.marginY;
                 pixelY + m_specification.tileHeight <= m_info.textureHeight - m_specification.marginY;
                 pixelY += m_specification.tileHeight + m_specification.paddingY)
            {
                std::uint32_t column = 0;
                for (std::uint32_t pixelX = m_specification.marginX;
                     pixelX + m_specification.tileWidth <= m_info.textureWidth - m_specification.marginX;
                     pixelX += m_specification.tileWidth + m_specification.paddingX)
                {
                    AddRegion(
                        m_specification.regionNamePrefix + "-" + std::to_string(column) + "-" + std::to_string(row),
                        pixelX,
                        pixelY,
                        m_specification.tileWidth,
                        m_specification.tileHeight);
                    ++column;
                }

                ++row;
            }

            VC_ASSERT(!m_regions.empty(), "DX11 texture atlas grid did not produce any regions.");
        }

        void BindPS(std::uint32_t slot)
        {
            m_texture.BindPS(slot);
            ++m_info.bindCount;
        }

    private:
        DX11Texture2D& m_texture;
        DX11TextureAtlasSpecification m_specification;
        DX11TextureAtlasInfo m_info;
        std::vector<DX11TextureAtlasRegion> m_regions;
    };

    DX11TextureAtlas::DX11TextureAtlas(DX11Texture2D& texture, DX11TextureAtlasSpecification specification)
        : m_impl(CreateScope<Impl>(texture, std::move(specification)))
    {
    }

    DX11TextureAtlas::~DX11TextureAtlas() = default;

    DX11TextureAtlas::DX11TextureAtlas(DX11TextureAtlas&& other) noexcept = default;

    DX11TextureAtlas& DX11TextureAtlas::operator=(DX11TextureAtlas&& other) noexcept = default;

    const DX11TextureAtlasSpecification& DX11TextureAtlas::GetSpecification() const noexcept
    {
        return m_impl->GetSpecification();
    }

    const DX11TextureAtlasInfo& DX11TextureAtlas::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    const DX11Texture2D& DX11TextureAtlas::GetTexture() const noexcept
    {
        return m_impl->GetTexture();
    }

    const std::vector<DX11TextureAtlasRegion>& DX11TextureAtlas::GetRegions() const noexcept
    {
        return m_impl->GetRegions();
    }

    bool DX11TextureAtlas::HasRegion(std::string_view name) const noexcept
    {
        return m_impl->HasRegion(name);
    }

    const DX11TextureAtlasRegion* DX11TextureAtlas::FindRegion(std::string_view name) const noexcept
    {
        return m_impl->FindRegion(name);
    }

    const DX11TextureAtlasRegion& DX11TextureAtlas::GetRegion(std::string_view name) const
    {
        return m_impl->GetRegion(name);
    }

    const DX11TextureAtlasRegion& DX11TextureAtlas::GetRegion(std::size_t index) const
    {
        return m_impl->GetRegion(index);
    }

    void DX11TextureAtlas::AddRegion(std::string name, std::uint32_t pixelX, std::uint32_t pixelY, std::uint32_t pixelWidth, std::uint32_t pixelHeight)
    {
        m_impl->AddRegion(std::move(name), pixelX, pixelY, pixelWidth, pixelHeight);
    }

    void DX11TextureAtlas::BuildUniformGrid()
    {
        m_impl->BuildUniformGrid();
    }

    void DX11TextureAtlas::BindPS(std::uint32_t slot)
    {
        m_impl->BindPS(slot);
    }
}
