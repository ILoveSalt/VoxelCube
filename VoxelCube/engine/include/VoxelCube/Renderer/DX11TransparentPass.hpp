#pragma once

#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    class DX11Device;
    class DX11PipelineState;
    class DX11RenderTargets;

    struct DX11TransparentPassInfo
    {
        std::uint64_t passCount = 0;
        std::string depthFunction = "Unknown";
        bool depthWritesEnabled = false;
        bool alphaBlendingRequired = true;
        bool ready = false;
    };

    class DX11TransparentPass
    {
    public:
        DX11TransparentPass(DX11Device& device, DX11RenderTargets& renderTargets);
        ~DX11TransparentPass();

        DX11TransparentPass(const DX11TransparentPass&) = delete;
        DX11TransparentPass& operator=(const DX11TransparentPass&) = delete;
        DX11TransparentPass(DX11TransparentPass&&) noexcept;
        DX11TransparentPass& operator=(DX11TransparentPass&&) noexcept;

        [[nodiscard]] const DX11TransparentPassInfo& GetInfo() const noexcept;

        void Begin(DX11PipelineState& pipelineState);
        void End();

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
