#pragma once

#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace vc
{
    struct DX11DeviceSpecification
    {
#if defined(NDEBUG)
        bool requestDebugLayer = false;
#else
        bool requestDebugLayer = true;
#endif
        bool preferHighPerformanceAdapter = true;
    };

    struct DX11AdapterInfo
    {
        std::string description = "Unknown";
        std::uint32_t vendorId = 0;
        std::uint32_t deviceId = 0;
        std::uint64_t dedicatedVideoMemory = 0;
        std::uint64_t dedicatedSystemMemory = 0;
        std::uint64_t sharedSystemMemory = 0;
        bool software = false;
    };

    struct DX11ImmediateContextInfo
    {
        std::string type = "Unknown";
        std::uint32_t flags = 0;
        bool available = false;
    };

    struct DX11DeviceInfo
    {
        DX11AdapterInfo adapter;
        std::string featureLevel = "Unknown";
        DX11ImmediateContextInfo immediateContext;
        bool debugLayerEnabled = false;
        bool warpDriver = false;
    };

    class DX11Device
    {
    public:
        explicit DX11Device(DX11DeviceSpecification specification = {});
        ~DX11Device();

        DX11Device(const DX11Device&) = delete;
        DX11Device& operator=(const DX11Device&) = delete;
        DX11Device(DX11Device&&) noexcept;
        DX11Device& operator=(DX11Device&&) noexcept;

        [[nodiscard]] const DX11DeviceSpecification& GetSpecification() const noexcept;
        [[nodiscard]] const DX11DeviceInfo& GetInfo() const noexcept;
        [[nodiscard]] ID3D11Device* GetNativeDevice() const noexcept;
        [[nodiscard]] ID3D11DeviceContext* GetImmediateContext() const noexcept;
        [[nodiscard]] bool HasImmediateContext() const noexcept;

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
