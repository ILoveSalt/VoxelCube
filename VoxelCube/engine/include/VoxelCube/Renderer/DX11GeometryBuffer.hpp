#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <VoxelCube/Core/Base.hpp>
#include <VoxelCube/Renderer/DX11Buffer.hpp>

namespace vc
{
    enum class DX11IndexFormat
    {
        UInt16 = 0,
        UInt32
    };

    struct DX11GeometryBufferSpecification
    {
        const void* vertexData = nullptr;
        std::uint32_t vertexCount = 0;
        std::uint32_t vertexStride = 0;
        const void* indexData = nullptr;
        std::uint32_t indexCount = 0;
        DX11IndexFormat indexFormat = DX11IndexFormat::UInt16;
        DX11BufferUsage usage = DX11BufferUsage::Immutable;
    };

    struct DX11GeometryBufferInfo
    {
        std::uint32_t vertexCount = 0;
        std::uint32_t indexCount = 0;
        std::uint32_t vertexStride = 0;
        std::uint32_t indexStride = 0;
        std::uint32_t vertexBufferSizeInBytes = 0;
        std::uint32_t indexBufferSizeInBytes = 0;
        std::uint64_t bindCount = 0;
        std::uint64_t drawCount = 0;
        std::string indexFormat = "Unknown";
        std::string usage = "Unknown";
        bool ready = false;
    };

    class DX11Device;

    class DX11GeometryBuffer
    {
    public:
        DX11GeometryBuffer(DX11Device& device, DX11GeometryBufferSpecification specification = {});
        ~DX11GeometryBuffer();

        DX11GeometryBuffer(const DX11GeometryBuffer&) = delete;
        DX11GeometryBuffer& operator=(const DX11GeometryBuffer&) = delete;
        DX11GeometryBuffer(DX11GeometryBuffer&&) noexcept;
        DX11GeometryBuffer& operator=(DX11GeometryBuffer&&) noexcept;

        [[nodiscard]] const DX11GeometryBufferSpecification& GetSpecification() const noexcept;
        [[nodiscard]] const DX11GeometryBufferInfo& GetInfo() const noexcept;
        [[nodiscard]] const DX11Buffer* GetVertexBuffer() const noexcept;
        [[nodiscard]] const DX11Buffer* GetIndexBuffer() const noexcept;

        void Bind(std::uint32_t vertexBufferSlot = 0);
        void DrawIndexed(std::uint32_t indexCount = 0, std::uint32_t startIndexLocation = 0, std::int32_t baseVertexLocation = 0);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };

    [[nodiscard]] std::string_view ToString(DX11IndexFormat indexFormat);
}
