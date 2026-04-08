#include <VoxelCube/Renderer/DX11GeometryBuffer.hpp>

#include <dxgi.h>
#include <d3d11.h>

#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Device.hpp>

namespace
{
    DXGI_FORMAT ToNativeIndexFormat(vc::DX11IndexFormat indexFormat)
    {
        switch (indexFormat)
        {
            case vc::DX11IndexFormat::UInt16:
                return DXGI_FORMAT_R16_UINT;
            case vc::DX11IndexFormat::UInt32:
                return DXGI_FORMAT_R32_UINT;
        }

        return DXGI_FORMAT_R16_UINT;
    }

    std::uint32_t GetIndexStride(vc::DX11IndexFormat indexFormat)
    {
        switch (indexFormat)
        {
            case vc::DX11IndexFormat::UInt16:
                return 2u;
            case vc::DX11IndexFormat::UInt32:
                return 4u;
        }

        return 2u;
    }
}

namespace vc
{
    std::string_view ToString(DX11IndexFormat indexFormat)
    {
        switch (indexFormat)
        {
            case DX11IndexFormat::UInt16:
                return "UInt16";
            case DX11IndexFormat::UInt32:
                return "UInt32";
        }

        return "Unknown";
    }

    class DX11GeometryBuffer::Impl
    {
    public:
        Impl(DX11Device& device, DX11GeometryBufferSpecification specification)
            : m_device(device)
            , m_specification(std::move(specification))
        {
            ValidateSpecification();
            Initialize();
        }

        [[nodiscard]] const DX11GeometryBufferSpecification& GetSpecification() const noexcept
        {
            return m_specification;
        }

        [[nodiscard]] const DX11GeometryBufferInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] const DX11Buffer* GetVertexBuffer() const noexcept
        {
            return m_vertexBuffer.get();
        }

        [[nodiscard]] const DX11Buffer* GetIndexBuffer() const noexcept
        {
            return m_indexBuffer.get();
        }

        void Bind(std::uint32_t vertexBufferSlot)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 geometry buffer requires an immediate context.");

            ID3D11Buffer* nativeVertexBuffer = m_vertexBuffer->GetNativeBuffer();
            VC_ASSERT(nativeVertexBuffer != nullptr, "DX11 geometry vertex buffer is not ready.");

            constexpr std::uint32_t vertexOffset = 0;
            const std::uint32_t vertexStride = m_specification.vertexStride;
            immediateContext->IASetVertexBuffers(
                vertexBufferSlot,
                1,
                &nativeVertexBuffer,
                &vertexStride,
                &vertexOffset);

            ID3D11Buffer* nativeIndexBuffer = m_indexBuffer->GetNativeBuffer();
            VC_ASSERT(nativeIndexBuffer != nullptr, "DX11 geometry index buffer is not ready.");
            immediateContext->IASetIndexBuffer(nativeIndexBuffer, ToNativeIndexFormat(m_specification.indexFormat), 0);
            ++m_info.bindCount;
        }

        void DrawIndexed(std::uint32_t indexCount, std::uint32_t startIndexLocation, std::int32_t baseVertexLocation)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 geometry buffer requires an immediate context.");

            const std::uint32_t resolvedIndexCount = indexCount > 0 ? indexCount : m_specification.indexCount;
            VC_ASSERT(resolvedIndexCount > 0, "DX11 geometry draw requires at least one index.");
            VC_ASSERT(
                startIndexLocation + resolvedIndexCount <= m_specification.indexCount,
                "DX11 geometry draw exceeds uploaded index buffer range.");

            immediateContext->DrawIndexed(resolvedIndexCount, startIndexLocation, baseVertexLocation);
            ++m_info.drawCount;
        }

    private:
        void ValidateSpecification() const
        {
            VC_ASSERT(m_specification.vertexData != nullptr, "DX11 geometry buffer requires vertex data.");
            VC_ASSERT(m_specification.vertexCount > 0, "DX11 geometry buffer requires at least one vertex.");
            VC_ASSERT(m_specification.vertexStride > 0, "DX11 geometry buffer requires a non-zero vertex stride.");
            VC_ASSERT(m_specification.indexData != nullptr, "DX11 geometry buffer requires index data.");
            VC_ASSERT(m_specification.indexCount > 0, "DX11 geometry buffer requires at least one index.");
            VC_ASSERT(
                m_specification.usage == DX11BufferUsage::Default || m_specification.usage == DX11BufferUsage::Immutable,
                "DX11 geometry buffer currently supports only Default or Immutable usage.");
        }

        void Initialize()
        {
            const std::uint32_t indexStride = GetIndexStride(m_specification.indexFormat);
            const std::uint32_t vertexBufferSize = m_specification.vertexCount * m_specification.vertexStride;
            const std::uint32_t indexBufferSize = m_specification.indexCount * indexStride;

            m_vertexBuffer = CreateScope<DX11Buffer>(
                m_device,
                DX11BufferSpecification {
                    .sizeInBytes = vertexBufferSize,
                    .stride = m_specification.vertexStride,
                    .kind = DX11BufferKind::Vertex,
                    .usage = m_specification.usage,
                    .cpuReadable = false,
                    .cpuWritable = false,
                    .initialData = m_specification.vertexData
                });
            m_indexBuffer = CreateScope<DX11Buffer>(
                m_device,
                DX11BufferSpecification {
                    .sizeInBytes = indexBufferSize,
                    .stride = indexStride,
                    .kind = DX11BufferKind::Index,
                    .usage = m_specification.usage,
                    .cpuReadable = false,
                    .cpuWritable = false,
                    .initialData = m_specification.indexData
                });

            m_info.vertexCount = m_specification.vertexCount;
            m_info.indexCount = m_specification.indexCount;
            m_info.vertexStride = m_specification.vertexStride;
            m_info.indexStride = indexStride;
            m_info.vertexBufferSizeInBytes = vertexBufferSize;
            m_info.indexBufferSizeInBytes = indexBufferSize;
            m_info.indexFormat = std::string(ToString(m_specification.indexFormat));
            m_info.usage = std::string(ToString(m_specification.usage));
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 geometry buffer created: vertices=" +
                std::to_string(m_info.vertexCount) +
                " (" +
                std::to_string(m_info.vertexBufferSizeInBytes) +
                " bytes), indices=" +
                std::to_string(m_info.indexCount) +
                " (" +
                std::to_string(m_info.indexBufferSizeInBytes) +
                " bytes), indexFormat=" +
                m_info.indexFormat +
                ", usage=" +
                m_info.usage);
        }

    private:
        DX11Device& m_device;
        DX11GeometryBufferSpecification m_specification;
        DX11GeometryBufferInfo m_info;
        Scope<DX11Buffer> m_vertexBuffer;
        Scope<DX11Buffer> m_indexBuffer;
    };

    DX11GeometryBuffer::DX11GeometryBuffer(DX11Device& device, DX11GeometryBufferSpecification specification)
        : m_impl(CreateScope<Impl>(device, std::move(specification)))
    {
    }

    DX11GeometryBuffer::~DX11GeometryBuffer() = default;

    DX11GeometryBuffer::DX11GeometryBuffer(DX11GeometryBuffer&& other) noexcept = default;

    DX11GeometryBuffer& DX11GeometryBuffer::operator=(DX11GeometryBuffer&& other) noexcept = default;

    const DX11GeometryBufferSpecification& DX11GeometryBuffer::GetSpecification() const noexcept
    {
        return m_impl->GetSpecification();
    }

    const DX11GeometryBufferInfo& DX11GeometryBuffer::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    const DX11Buffer* DX11GeometryBuffer::GetVertexBuffer() const noexcept
    {
        return m_impl->GetVertexBuffer();
    }

    const DX11Buffer* DX11GeometryBuffer::GetIndexBuffer() const noexcept
    {
        return m_impl->GetIndexBuffer();
    }

    void DX11GeometryBuffer::Bind(std::uint32_t vertexBufferSlot)
    {
        m_impl->Bind(vertexBufferSlot);
    }

    void DX11GeometryBuffer::DrawIndexed(std::uint32_t indexCount, std::uint32_t startIndexLocation, std::int32_t baseVertexLocation)
    {
        m_impl->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
    }
}
