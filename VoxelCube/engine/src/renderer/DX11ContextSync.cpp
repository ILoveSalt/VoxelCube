#include <VoxelCube/Renderer/DX11ContextSync.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
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
}

namespace vc
{
    class DX11ContextSync::Impl
    {
    public:
        explicit Impl(DX11Device& device)
            : m_device(device)
        {
            Initialize();
        }

        [[nodiscard]] const DX11ContextSyncInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        void Flush()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 context sync requires an immediate context.");

            immediateContext->Flush();
            ++m_info.flushCount;
        }

        [[nodiscard]] bool WaitForGpuIdle(std::uint32_t timeoutMilliseconds)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 context sync requires an immediate context.");
            VC_ASSERT(m_gpuIdleQuery != nullptr, "DX11 context sync requires a valid event query.");

            const auto waitStartTime = std::chrono::steady_clock::now();
            immediateContext->End(m_gpuIdleQuery.Get());
            Flush();

            for (;;)
            {
                const HRESULT result = immediateContext->GetData(m_gpuIdleQuery.Get(), nullptr, 0, 0);
                if (result == S_OK)
                {
                    const auto waitEndTime = std::chrono::steady_clock::now();
                    const double waitDurationMilliseconds =
                        std::chrono::duration<double, std::milli>(waitEndTime - waitStartTime).count();
                    ++m_info.waitCount;
                    m_info.lastWaitDurationMilliseconds = waitDurationMilliseconds;
                    m_info.maxWaitDurationMilliseconds =
                        std::max(m_info.maxWaitDurationMilliseconds, waitDurationMilliseconds);
                    return true;
                }

                VC_ASSERT(
                    result == S_FALSE,
                    ("Failed while waiting for D3D11 GPU idle query: " + FormatHRESULT(result)).c_str());

                const auto currentTime = std::chrono::steady_clock::now();
                const auto elapsedMilliseconds =
                    std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - waitStartTime).count();
                if (elapsedMilliseconds >= static_cast<long long>(timeoutMilliseconds))
                {
                    ++m_info.waitCount;
                    ++m_info.waitTimeoutCount;
                    m_info.lastWaitDurationMilliseconds = static_cast<double>(elapsedMilliseconds);
                    m_info.maxWaitDurationMilliseconds =
                        std::max(m_info.maxWaitDurationMilliseconds, m_info.lastWaitDurationMilliseconds);
                    VC_LOG_WARN(
                        "DX11 context sync timed out while waiting for GPU idle after " +
                        std::to_string(timeoutMilliseconds) +
                        " ms.");
                    return false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

    private:
        void Initialize()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 context sync requires a ready D3D11 device.");

            D3D11_QUERY_DESC queryDescription {};
            queryDescription.Query = D3D11_QUERY_EVENT;
            queryDescription.MiscFlags = 0;

            const HRESULT result = nativeDevice->CreateQuery(&queryDescription, &m_gpuIdleQuery);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create D3D11 event query for context sync: " + FormatHRESULT(result)).c_str());

            m_info.queryReady = true;
            VC_LOG_INFO("DX11 context sync initialized with D3D11 event query.");
        }

    private:
        DX11Device& m_device;
        DX11ContextSyncInfo m_info;
        ComPtr<ID3D11Query> m_gpuIdleQuery;
    };

    DX11ContextSync::DX11ContextSync(DX11Device& device)
        : m_impl(CreateScope<Impl>(device))
    {
    }

    DX11ContextSync::~DX11ContextSync() = default;

    DX11ContextSync::DX11ContextSync(DX11ContextSync&& other) noexcept = default;

    DX11ContextSync& DX11ContextSync::operator=(DX11ContextSync&& other) noexcept = default;

    const DX11ContextSyncInfo& DX11ContextSync::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    void DX11ContextSync::Flush()
    {
        m_impl->Flush();
    }

    bool DX11ContextSync::WaitForGpuIdle(std::uint32_t timeoutMilliseconds)
    {
        return m_impl->WaitForGpuIdle(timeoutMilliseconds);
    }
}
