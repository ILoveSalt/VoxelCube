#include <VoxelCube/Renderer/DX11Buffer.hpp>

#include <cstring>
#include <iomanip>
#include <sstream>
#include <utility>

#include <d3d11.h>
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

    D3D11_USAGE ToNativeUsage(vc::DX11BufferUsage usage)
    {
        switch (usage)
        {
            case vc::DX11BufferUsage::Default:
                return D3D11_USAGE_DEFAULT;
            case vc::DX11BufferUsage::Immutable:
                return D3D11_USAGE_IMMUTABLE;
            case vc::DX11BufferUsage::Dynamic:
                return D3D11_USAGE_DYNAMIC;
            case vc::DX11BufferUsage::Staging:
                return D3D11_USAGE_STAGING;
        }

        return D3D11_USAGE_DEFAULT;
    }

    UINT ToNativeBindFlags(vc::DX11BufferKind kind, vc::DX11BufferUsage usage)
    {
        if (usage == vc::DX11BufferUsage::Staging)
        {
            return 0;
        }

        switch (kind)
        {
            case vc::DX11BufferKind::Generic:
                return 0;
            case vc::DX11BufferKind::Vertex:
                return D3D11_BIND_VERTEX_BUFFER;
            case vc::DX11BufferKind::Index:
                return D3D11_BIND_INDEX_BUFFER;
            case vc::DX11BufferKind::Constant:
                return D3D11_BIND_CONSTANT_BUFFER;
        }

        return 0;
    }

    UINT ToNativeCpuAccessFlags(bool cpuReadable, bool cpuWritable)
    {
        UINT flags = 0;
        if (cpuReadable)
        {
            flags |= D3D11_CPU_ACCESS_READ;
        }

        if (cpuWritable)
        {
            flags |= D3D11_CPU_ACCESS_WRITE;
        }

        return flags;
    }

    D3D11_MAP ToNativeMapMode(vc::DX11BufferMapMode mapMode)
    {
        switch (mapMode)
        {
            case vc::DX11BufferMapMode::Read:
                return D3D11_MAP_READ;
            case vc::DX11BufferMapMode::Write:
                return D3D11_MAP_WRITE;
            case vc::DX11BufferMapMode::ReadWrite:
                return D3D11_MAP_READ_WRITE;
            case vc::DX11BufferMapMode::WriteDiscard:
                return D3D11_MAP_WRITE_DISCARD;
            case vc::DX11BufferMapMode::WriteNoOverwrite:
                return D3D11_MAP_WRITE_NO_OVERWRITE;
        }

        return D3D11_MAP_READ;
    }
}

namespace vc
{
    std::string_view ToString(DX11BufferKind kind)
    {
        switch (kind)
        {
            case DX11BufferKind::Generic:
                return "Generic";
            case DX11BufferKind::Vertex:
                return "Vertex";
            case DX11BufferKind::Index:
                return "Index";
            case DX11BufferKind::Constant:
                return "Constant";
        }

        return "Unknown";
    }

    std::string_view ToString(DX11BufferUsage usage)
    {
        switch (usage)
        {
            case DX11BufferUsage::Default:
                return "Default";
            case DX11BufferUsage::Immutable:
                return "Immutable";
            case DX11BufferUsage::Dynamic:
                return "Dynamic";
            case DX11BufferUsage::Staging:
                return "Staging";
        }

        return "Unknown";
    }

    std::string_view ToString(DX11BufferMapMode mapMode)
    {
        switch (mapMode)
        {
            case DX11BufferMapMode::Read:
                return "Read";
            case DX11BufferMapMode::Write:
                return "Write";
            case DX11BufferMapMode::ReadWrite:
                return "ReadWrite";
            case DX11BufferMapMode::WriteDiscard:
                return "WriteDiscard";
            case DX11BufferMapMode::WriteNoOverwrite:
                return "WriteNoOverwrite";
        }

        return "Unknown";
    }

    class DX11Buffer::Impl
    {
    public:
        Impl(DX11Device& device, DX11BufferSpecification specification)
            : m_device(device)
            , m_specification(std::move(specification))
        {
            ValidateSpecification();
            Initialize();
        }

        [[nodiscard]] const DX11BufferSpecification& GetSpecification() const noexcept
        {
            return m_specification;
        }

        [[nodiscard]] const DX11BufferInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] ID3D11Buffer* GetNativeBuffer() const noexcept
        {
            return m_buffer.Get();
        }

        [[nodiscard]] DX11MappedBuffer Map(DX11BufferMapMode mapMode)
        {
            VC_ASSERT(m_buffer != nullptr, "DX11 buffer is not initialized.");
            VC_ASSERT(!m_info.mapped, "DX11 buffer is already mapped.");
            ValidateMapMode(mapMode);

            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 buffer requires an immediate context.");

            D3D11_MAPPED_SUBRESOURCE mappedResource {};
            const HRESULT result = immediateContext->Map(
                m_buffer.Get(),
                0,
                ToNativeMapMode(mapMode),
                0,
                &mappedResource);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to map DX11 buffer: " + FormatHRESULT(result)).c_str());

            ++m_info.mapCount;
            m_info.lastMapMode = std::string(ToString(mapMode));
            m_info.mapped = true;

            return {
                .data = mappedResource.pData,
                .sizeInBytes = m_specification.sizeInBytes
            };
        }

        void Unmap()
        {
            VC_ASSERT(m_buffer != nullptr, "DX11 buffer is not initialized.");
            VC_ASSERT(m_info.mapped, "DX11 buffer is not currently mapped.");

            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 buffer requires an immediate context.");

            immediateContext->Unmap(m_buffer.Get(), 0);
            m_info.mapped = false;
        }

        void Write(const void* data, std::uint32_t sizeInBytes, std::uint32_t destinationOffset)
        {
            VC_ASSERT(data != nullptr, "DX11 buffer write requires source data.");
            VC_ASSERT(sizeInBytes > 0, "DX11 buffer write size must be greater than zero.");
            VC_ASSERT(!m_info.mapped, "DX11 buffer cannot be written while mapped.");
            VC_ASSERT(
                destinationOffset + sizeInBytes <= m_specification.sizeInBytes,
                "DX11 buffer write exceeds the buffer size.");

            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 buffer requires an immediate context.");

            if (m_specification.usage == DX11BufferUsage::Immutable)
            {
                VC_ASSERT(false, "Immutable DX11 buffers cannot be updated after creation.");
            }

            if (m_specification.usage == DX11BufferUsage::Default)
            {
                D3D11_BOX destinationRegion {};
                destinationRegion.left = destinationOffset;
                destinationRegion.right = destinationOffset + sizeInBytes;
                destinationRegion.top = 0;
                destinationRegion.bottom = 1;
                destinationRegion.front = 0;
                destinationRegion.back = 1;

                immediateContext->UpdateSubresource(m_buffer.Get(), 0, &destinationRegion, data, 0, 0);
            }
            else
            {
                DX11BufferMapMode mapMode = DX11BufferMapMode::Write;
                if (m_specification.usage == DX11BufferUsage::Dynamic)
                {
                    VC_ASSERT(
                        destinationOffset == 0 && sizeInBytes == m_specification.sizeInBytes,
                        "Dynamic DX11 buffer Write currently requires whole-buffer updates.");
                    mapMode = DX11BufferMapMode::WriteDiscard;
                }
                else if (m_specification.usage == DX11BufferUsage::Staging)
                {
                    mapMode = DX11BufferMapMode::Write;
                }

                const DX11MappedBuffer mappedBuffer = Map(mapMode);
                auto* destinationBytes = static_cast<std::uint8_t*>(mappedBuffer.data);
                std::memcpy(destinationBytes + destinationOffset, data, sizeInBytes);
                Unmap();
            }

            ++m_info.writeCount;
        }

        void CopyFrom(const DX11Buffer& source)
        {
            VC_ASSERT(m_buffer != nullptr, "DX11 buffer is not initialized.");
            VC_ASSERT(source.GetNativeBuffer() != nullptr, "DX11 source buffer is not initialized.");
            VC_ASSERT(!m_info.mapped, "DX11 buffer cannot be copied into while mapped.");
            VC_ASSERT(!source.GetInfo().mapped, "DX11 source buffer cannot be copied from while mapped.");
            VC_ASSERT(
                m_specification.sizeInBytes == source.GetSpecification().sizeInBytes,
                "DX11 buffer copy requires matching source and destination sizes.");

            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 buffer requires an immediate context.");

            immediateContext->CopyResource(m_buffer.Get(), source.GetNativeBuffer());
            ++m_info.copyCount;
        }

    private:
        void ValidateSpecification()
        {
            VC_ASSERT(m_specification.sizeInBytes > 0, "DX11 buffer size must be greater than zero.");

            if (m_specification.kind == DX11BufferKind::Constant)
            {
                VC_ASSERT(
                    (m_specification.sizeInBytes % 16u) == 0u,
                    "DX11 constant buffer size must be a multiple of 16 bytes.");
            }

            switch (m_specification.usage)
            {
                case DX11BufferUsage::Default:
                    VC_ASSERT(
                        !m_specification.cpuReadable && !m_specification.cpuWritable,
                        "Default DX11 buffers do not support CPU read/write access.");
                    break;

                case DX11BufferUsage::Immutable:
                    VC_ASSERT(
                        !m_specification.cpuReadable && !m_specification.cpuWritable,
                        "Immutable DX11 buffers do not support CPU read/write access.");
                    VC_ASSERT(
                        m_specification.initialData != nullptr,
                        "Immutable DX11 buffers require initial data.");
                    break;

                case DX11BufferUsage::Dynamic:
                    VC_ASSERT(
                        m_specification.cpuWritable && !m_specification.cpuReadable,
                        "Dynamic DX11 buffers require CPU write access and do not support CPU read access.");
                    break;

                case DX11BufferUsage::Staging:
                    VC_ASSERT(
                        m_specification.kind == DX11BufferKind::Generic,
                        "Staging DX11 buffers must use the Generic kind.");
                    VC_ASSERT(
                        m_specification.cpuReadable || m_specification.cpuWritable,
                        "Staging DX11 buffers require CPU read or write access.");
                    break;
            }
        }

        void ValidateMapMode(DX11BufferMapMode mapMode) const
        {
            switch (mapMode)
            {
                case DX11BufferMapMode::Read:
                    VC_ASSERT(m_specification.cpuReadable, "DX11 buffer is not CPU-readable.");
                    break;

                case DX11BufferMapMode::Write:
                    VC_ASSERT(m_specification.cpuWritable, "DX11 buffer is not CPU-writable.");
                    VC_ASSERT(
                        m_specification.usage == DX11BufferUsage::Staging,
                        "DX11 MAP_WRITE is currently supported only for staging buffers.");
                    break;

                case DX11BufferMapMode::ReadWrite:
                    VC_ASSERT(
                        m_specification.cpuReadable && m_specification.cpuWritable,
                        "DX11 buffer requires CPU read/write access for MAP_READ_WRITE.");
                    VC_ASSERT(
                        m_specification.usage == DX11BufferUsage::Staging,
                        "DX11 MAP_READ_WRITE is currently supported only for staging buffers.");
                    break;

                case DX11BufferMapMode::WriteDiscard:
                case DX11BufferMapMode::WriteNoOverwrite:
                    VC_ASSERT(m_specification.cpuWritable, "DX11 buffer is not CPU-writable.");
                    VC_ASSERT(
                        m_specification.usage == DX11BufferUsage::Dynamic,
                        "DX11 write-discard/no-overwrite mapping requires a dynamic buffer.");
                    break;
            }
        }

        void Initialize()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 buffer requires a ready D3D11 device.");

            D3D11_BUFFER_DESC description {};
            description.ByteWidth = m_specification.sizeInBytes;
            description.Usage = ToNativeUsage(m_specification.usage);
            description.BindFlags = ToNativeBindFlags(m_specification.kind, m_specification.usage);
            description.CPUAccessFlags = ToNativeCpuAccessFlags(m_specification.cpuReadable, m_specification.cpuWritable);
            description.MiscFlags = 0;
            description.StructureByteStride = 0;

            D3D11_SUBRESOURCE_DATA initialData {};
            initialData.pSysMem = m_specification.initialData;

            const HRESULT result = nativeDevice->CreateBuffer(
                &description,
                m_specification.initialData != nullptr ? &initialData : nullptr,
                &m_buffer);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 buffer: " + FormatHRESULT(result)).c_str());

            m_info.sizeInBytes = m_specification.sizeInBytes;
            m_info.stride = m_specification.stride;
            m_info.kind = std::string(ToString(m_specification.kind));
            m_info.usage = std::string(ToString(m_specification.usage));
            m_info.cpuReadable = m_specification.cpuReadable;
            m_info.cpuWritable = m_specification.cpuWritable;
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 buffer created: kind=" +
                m_info.kind +
                ", usage=" +
                m_info.usage +
                ", size=" +
                std::to_string(m_info.sizeInBytes) +
                " bytes, stride=" +
                std::to_string(m_info.stride) +
                ", cpuRead=" +
                std::string(m_info.cpuReadable ? "true" : "false") +
                ", cpuWrite=" +
                std::string(m_info.cpuWritable ? "true" : "false"));
        }

    private:
        DX11Device& m_device;
        DX11BufferSpecification m_specification;
        DX11BufferInfo m_info;
        ComPtr<ID3D11Buffer> m_buffer;
    };

    DX11Buffer::DX11Buffer(DX11Device& device, DX11BufferSpecification specification)
        : m_impl(CreateScope<Impl>(device, std::move(specification)))
    {
    }

    DX11Buffer::~DX11Buffer() = default;

    DX11Buffer::DX11Buffer(DX11Buffer&& other) noexcept = default;

    DX11Buffer& DX11Buffer::operator=(DX11Buffer&& other) noexcept = default;

    const DX11BufferSpecification& DX11Buffer::GetSpecification() const noexcept
    {
        return m_impl->GetSpecification();
    }

    const DX11BufferInfo& DX11Buffer::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    ID3D11Buffer* DX11Buffer::GetNativeBuffer() const noexcept
    {
        return m_impl->GetNativeBuffer();
    }

    DX11MappedBuffer DX11Buffer::Map(DX11BufferMapMode mapMode)
    {
        return m_impl->Map(mapMode);
    }

    void DX11Buffer::Unmap()
    {
        m_impl->Unmap();
    }

    void DX11Buffer::Write(const void* data, std::uint32_t sizeInBytes, std::uint32_t destinationOffset)
    {
        m_impl->Write(data, sizeInBytes, destinationOffset);
    }

    void DX11Buffer::CopyFrom(const DX11Buffer& source)
    {
        m_impl->CopyFrom(source);
    }
}
