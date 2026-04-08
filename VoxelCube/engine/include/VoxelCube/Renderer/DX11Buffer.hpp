#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <VoxelCube/Core/Base.hpp>

struct ID3D11Buffer;

namespace vc
{
    class DX11Device;

    enum class DX11BufferKind
    {
        Generic = 0,
        Vertex,
        Index,
        Constant
    };

    enum class DX11BufferUsage
    {
        Default = 0,
        Immutable,
        Dynamic,
        Staging
    };

    enum class DX11BufferMapMode
    {
        Read = 0,
        Write,
        ReadWrite,
        WriteDiscard,
        WriteNoOverwrite
    };

    struct DX11BufferSpecification
    {
        std::uint32_t sizeInBytes = 0;
        std::uint32_t stride = 0;
        DX11BufferKind kind = DX11BufferKind::Generic;
        DX11BufferUsage usage = DX11BufferUsage::Default;
        bool cpuReadable = false;
        bool cpuWritable = false;
        const void* initialData = nullptr;
    };

    struct DX11MappedBuffer
    {
        void* data = nullptr;
        std::uint32_t sizeInBytes = 0;
    };

    struct DX11BufferInfo
    {
        std::uint32_t sizeInBytes = 0;
        std::uint32_t stride = 0;
        std::uint64_t mapCount = 0;
        std::uint64_t writeCount = 0;
        std::uint64_t copyCount = 0;
        std::string kind = "Generic";
        std::string usage = "Unknown";
        std::string lastMapMode = "None";
        bool cpuReadable = false;
        bool cpuWritable = false;
        bool mapped = false;
        bool ready = false;
    };

    class DX11Buffer
    {
    public:
        DX11Buffer(DX11Device& device, DX11BufferSpecification specification = {});
        ~DX11Buffer();

        DX11Buffer(const DX11Buffer&) = delete;
        DX11Buffer& operator=(const DX11Buffer&) = delete;
        DX11Buffer(DX11Buffer&&) noexcept;
        DX11Buffer& operator=(DX11Buffer&&) noexcept;

        [[nodiscard]] const DX11BufferSpecification& GetSpecification() const noexcept;
        [[nodiscard]] const DX11BufferInfo& GetInfo() const noexcept;
        [[nodiscard]] ID3D11Buffer* GetNativeBuffer() const noexcept;

        [[nodiscard]] DX11MappedBuffer Map(DX11BufferMapMode mapMode);
        void Unmap();
        void Write(const void* data, std::uint32_t sizeInBytes, std::uint32_t destinationOffset = 0);
        void CopyFrom(const DX11Buffer& source);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };

    [[nodiscard]] std::string_view ToString(DX11BufferKind kind);
    [[nodiscard]] std::string_view ToString(DX11BufferUsage usage);
    [[nodiscard]] std::string_view ToString(DX11BufferMapMode mapMode);
}
