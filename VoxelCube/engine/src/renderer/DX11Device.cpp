#include <VoxelCube/Renderer/DX11Device.hpp>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <VoxelCube/Core/Log.hpp>

namespace
{
    using Microsoft::WRL::ComPtr;

    struct AdapterSelection
    {
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 description {};
        bool warpFallback = false;
    };

    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || value[0] == L'\0')
        {
            return {};
        }

        const int requiredCharacters = WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        VC_ASSERT(requiredCharacters > 0, "Failed to determine UTF-8 buffer size.");

        std::string result(static_cast<std::size_t>(requiredCharacters - 1), '\0');
        const int writtenCharacters = WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            result.data(),
            requiredCharacters,
            nullptr,
            nullptr);
        VC_ASSERT(writtenCharacters == requiredCharacters, "Failed to convert UTF-16 string to UTF-8.");
        return result;
    }

    std::string FormatHRESULT(HRESULT result)
    {
        std::ostringstream stream;
        stream << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0')
               << static_cast<std::uint32_t>(result);
        return stream.str();
    }

    std::string FeatureLevelToString(D3D_FEATURE_LEVEL featureLevel)
    {
        switch (featureLevel)
        {
            case D3D_FEATURE_LEVEL_11_1:
                return "11.1";
            case D3D_FEATURE_LEVEL_11_0:
                return "11.0";
            case D3D_FEATURE_LEVEL_10_1:
                return "10.1";
            case D3D_FEATURE_LEVEL_10_0:
                return "10.0";
            default:
                return "Unknown";
        }
    }

    std::string FormatMemoryMiB(std::uint64_t bytes)
    {
        return std::to_string(bytes / (1024ull * 1024ull)) + " MiB";
    }

    std::string ContextTypeToString(D3D11_DEVICE_CONTEXT_TYPE contextType)
    {
        switch (contextType)
        {
            case D3D11_DEVICE_CONTEXT_IMMEDIATE:
                return "Immediate";
            case D3D11_DEVICE_CONTEXT_DEFERRED:
                return "Deferred";
            default:
                return "Unknown";
        }
    }

    AdapterSelection SelectAdapter(IDXGIFactory1* factory, bool preferHighPerformanceAdapter)
    {
        AdapterSelection bestSelection;
        std::uint64_t bestDedicatedVideoMemory = 0;

        for (UINT adapterIndex = 0;; ++adapterIndex)
        {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT result = factory->EnumAdapters1(adapterIndex, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            VC_ASSERT(SUCCEEDED(result), "Failed to enumerate DXGI adapters.");

            DXGI_ADAPTER_DESC1 description {};
            const HRESULT descriptionResult = adapter->GetDesc1(&description);
            VC_ASSERT(SUCCEEDED(descriptionResult), "Failed to query DXGI adapter description.");

            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                continue;
            }

            if (bestSelection.adapter == nullptr
                || (preferHighPerformanceAdapter && description.DedicatedVideoMemory > bestDedicatedVideoMemory))
            {
                bestSelection.adapter = adapter;
                bestSelection.description = description;
                bestSelection.warpFallback = false;
                bestDedicatedVideoMemory = description.DedicatedVideoMemory;
            }

            if (!preferHighPerformanceAdapter)
            {
                break;
            }
        }

        if (bestSelection.adapter == nullptr)
        {
            bestSelection.warpFallback = true;
            bestSelection.description.Description[0] = L'\0';
        }

        return bestSelection;
    }

    HRESULT CreateDevice(
        IDXGIAdapter1* adapter,
        D3D_DRIVER_TYPE driverType,
        UINT creationFlags,
        ID3D11Device** device,
        D3D_FEATURE_LEVEL* featureLevel,
        ID3D11DeviceContext** immediateContext)
    {
        D3D_FEATURE_LEVEL requestedFeatureLevels[] {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        HRESULT result = D3D11CreateDevice(
            adapter,
            driverType,
            nullptr,
            creationFlags,
            requestedFeatureLevels,
            static_cast<UINT>(std::size(requestedFeatureLevels)),
            D3D11_SDK_VERSION,
            device,
            featureLevel,
            immediateContext);

        if (result == E_INVALIDARG)
        {
            D3D_FEATURE_LEVEL fallbackFeatureLevels[] {
                D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1,
                D3D_FEATURE_LEVEL_10_0
            };

            result = D3D11CreateDevice(
                adapter,
                driverType,
                nullptr,
                creationFlags,
                fallbackFeatureLevels,
                static_cast<UINT>(std::size(fallbackFeatureLevels)),
                D3D11_SDK_VERSION,
                device,
                featureLevel,
                immediateContext);
        }

        return result;
    }
}

namespace vc
{
    class DX11Device::Impl
    {
    public:
        explicit Impl(DX11DeviceSpecification specification)
            : m_specification(std::move(specification))
        {
            Initialize();
        }

        [[nodiscard]] const DX11DeviceSpecification& GetSpecification() const noexcept
        {
            return m_specification;
        }

        [[nodiscard]] const DX11DeviceInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        [[nodiscard]] ID3D11Device* GetNativeDevice() const noexcept
        {
            return m_device.Get();
        }

        [[nodiscard]] ID3D11DeviceContext* GetImmediateContext() const noexcept
        {
            return m_immediateContext.Get();
        }

        [[nodiscard]] bool HasImmediateContext() const noexcept
        {
            return m_immediateContext != nullptr;
        }

    private:
        void Initialize()
        {
            const HRESULT factoryResult = CreateDXGIFactory1(IID_PPV_ARGS(&m_factory));
            VC_ASSERT(SUCCEEDED(factoryResult), "Failed to create DXGI factory.");

            const AdapterSelection selection = SelectAdapter(m_factory.Get(), m_specification.preferHighPerformanceAdapter);
            m_adapter = selection.adapter;

            if (!selection.warpFallback)
            {
                m_info.adapter.description = WideToUtf8(selection.description.Description);
                m_info.adapter.vendorId = selection.description.VendorId;
                m_info.adapter.deviceId = selection.description.DeviceId;
                m_info.adapter.dedicatedVideoMemory = selection.description.DedicatedVideoMemory;
                m_info.adapter.dedicatedSystemMemory = selection.description.DedicatedSystemMemory;
                m_info.adapter.sharedSystemMemory = selection.description.SharedSystemMemory;
                m_info.adapter.software = false;
            }
            else
            {
                m_info.adapter.description = "Microsoft WARP";
                m_info.adapter.software = true;
                m_info.warpDriver = true;
            }

            const D3D_DRIVER_TYPE driverType = selection.warpFallback ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_UNKNOWN;

            UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
            if (m_specification.requestDebugLayer)
            {
                creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
            }

            D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
            HRESULT deviceResult = CreateDevice(
                m_adapter.Get(),
                driverType,
                creationFlags,
                &m_device,
                &featureLevel,
                &m_immediateContext);
            if (FAILED(deviceResult) && m_specification.requestDebugLayer)
            {
                VC_LOG_WARN(
                    "DX11 debug layer was requested but device creation failed with " +
                    FormatHRESULT(deviceResult) +
                    "; retrying without the debug layer.");
                creationFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
                m_device.Reset();
                m_immediateContext.Reset();
                deviceResult = CreateDevice(
                    m_adapter.Get(),
                    driverType,
                    creationFlags,
                    &m_device,
                    &featureLevel,
                    &m_immediateContext);
            }

            VC_ASSERT(
                SUCCEEDED(deviceResult),
                ("Failed to create D3D11 device: " + FormatHRESULT(deviceResult)).c_str());
            VC_ASSERT(m_immediateContext != nullptr, "Failed to acquire D3D11 immediate context.");

            m_info.featureLevel = FeatureLevelToString(featureLevel);
            m_info.debugLayerEnabled = (creationFlags & D3D11_CREATE_DEVICE_DEBUG) != 0;
            m_info.immediateContext.available = true;
            m_info.immediateContext.type = ContextTypeToString(m_immediateContext->GetType());
            m_info.immediateContext.flags = m_immediateContext->GetContextFlags();

            VC_LOG_INFO(
                "DX11 device created: adapter=" +
                m_info.adapter.description +
                ", driver=" +
                std::string(m_info.warpDriver ? "WARP" : "Hardware") +
                ", featureLevel=" +
                m_info.featureLevel +
                ", context=" +
                m_info.immediateContext.type +
                ", debugLayer=" +
                std::string(m_info.debugLayerEnabled ? "enabled" : "disabled") +
                ", VRAM=" +
                FormatMemoryMiB(m_info.adapter.dedicatedVideoMemory));
        }

    private:
        DX11DeviceSpecification m_specification;
        DX11DeviceInfo m_info;
        ComPtr<IDXGIFactory1> m_factory;
        ComPtr<IDXGIAdapter1> m_adapter;
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_immediateContext;
    };

    DX11Device::DX11Device(DX11DeviceSpecification specification)
        : m_impl(CreateScope<Impl>(std::move(specification)))
    {
    }

    DX11Device::~DX11Device() = default;

    DX11Device::DX11Device(DX11Device&& other) noexcept = default;

    DX11Device& DX11Device::operator=(DX11Device&& other) noexcept = default;

    const DX11DeviceSpecification& DX11Device::GetSpecification() const noexcept
    {
        return m_impl->GetSpecification();
    }

    const DX11DeviceInfo& DX11Device::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    ID3D11Device* DX11Device::GetNativeDevice() const noexcept
    {
        return m_impl->GetNativeDevice();
    }

    ID3D11DeviceContext* DX11Device::GetImmediateContext() const noexcept
    {
        return m_impl->GetImmediateContext();
    }

    bool DX11Device::HasImmediateContext() const noexcept
    {
        return m_impl->HasImmediateContext();
    }
}
