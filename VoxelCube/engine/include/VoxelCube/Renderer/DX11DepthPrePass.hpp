#pragma once

#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    class DX11Device;
    class DX11PipelineState;
    class DX11RenderTargets;

    struct DX11DepthPrePassInfo
    {
        std::uint64_t depthPassCount = 0;
        std::uint64_t colorPassCount = 0;
        std::string prePassDepthFunction = "Unknown";
        std::string colorPassDepthFunction = "Unknown";
        bool ready = false;
    };

    class DX11DepthPrePass
    {
    public:
        DX11DepthPrePass(DX11Device& device, DX11RenderTargets& renderTargets);
        ~DX11DepthPrePass();

        DX11DepthPrePass(const DX11DepthPrePass&) = delete;
        DX11DepthPrePass& operator=(const DX11DepthPrePass&) = delete;
        DX11DepthPrePass(DX11DepthPrePass&&) noexcept;
        DX11DepthPrePass& operator=(DX11DepthPrePass&&) noexcept;

        [[nodiscard]] const DX11DepthPrePassInfo& GetInfo() const noexcept;

        void BeginDepthPass(DX11PipelineState& pipelineState);
        void BeginColorPass(DX11PipelineState& pipelineState);

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
