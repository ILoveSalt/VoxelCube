#pragma once

#include <cstdint>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    class DX11Device;

    struct DX11ContextSyncInfo
    {
        std::uint64_t flushCount = 0;
        std::uint64_t waitCount = 0;
        std::uint64_t waitTimeoutCount = 0;
        double lastWaitDurationMilliseconds = 0.0;
        double maxWaitDurationMilliseconds = 0.0;
        bool queryReady = false;
    };

    class DX11ContextSync
    {
    public:
        explicit DX11ContextSync(DX11Device& device);
        ~DX11ContextSync();

        DX11ContextSync(const DX11ContextSync&) = delete;
        DX11ContextSync& operator=(const DX11ContextSync&) = delete;
        DX11ContextSync(DX11ContextSync&&) noexcept;
        DX11ContextSync& operator=(DX11ContextSync&&) noexcept;

        [[nodiscard]] const DX11ContextSyncInfo& GetInfo() const noexcept;

        void Flush();
        [[nodiscard]] bool WaitForGpuIdle(std::uint32_t timeoutMilliseconds = 2000);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
