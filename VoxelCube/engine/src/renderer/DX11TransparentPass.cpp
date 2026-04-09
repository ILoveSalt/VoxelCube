#include <VoxelCube/Renderer/DX11TransparentPass.hpp>

#include <iomanip>
#include <sstream>
#include <utility>

#include <d3d11.h>
#include <wrl/client.h>

#include <VoxelCube/Core/Log.hpp>
#include <VoxelCube/Renderer/DX11Device.hpp>
#include <VoxelCube/Renderer/DX11PipelineState.hpp>
#include <VoxelCube/Renderer/DX11RenderTargets.hpp>

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
    class DX11TransparentPass::Impl
    {
    public:
        Impl(DX11Device& device, DX11RenderTargets& renderTargets)
            : m_device(device)
            , m_renderTargets(renderTargets)
        {
            CreateDepthStencilState();
            m_info.depthFunction = "LessEqual";
            m_info.depthWritesEnabled = false;
            m_info.alphaBlendingRequired = true;
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 transparent pass created: depth=" +
                m_info.depthFunction +
                ", depthWrites=" +
                std::string(m_info.depthWritesEnabled ? "enabled" : "disabled") +
                ", alphaBlend=required");
        }

        [[nodiscard]] const DX11TransparentPassInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        void Begin(DX11PipelineState& pipelineState)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 transparent pass requires an immediate context.");
            VC_ASSERT(
                pipelineState.GetInfo().alphaBlendingEnabled,
                "DX11 transparent pass expects an alpha-blended pipeline state.");

            m_renderTargets.Bind();
            pipelineState.Bind();
            immediateContext->OMSetDepthStencilState(m_depthReadOnlyState.Get(), 0);
            ++m_info.passCount;
        }

        void End()
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 transparent pass requires an immediate context.");

            immediateContext->OMSetDepthStencilState(nullptr, 0);
        }

    private:
        void CreateDepthStencilState()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 transparent pass requires a ready D3D11 device.");

            D3D11_DEPTH_STENCIL_DESC description {};
            description.DepthEnable = TRUE;
            description.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            description.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
            description.StencilEnable = FALSE;

            const HRESULT result = nativeDevice->CreateDepthStencilState(&description, &m_depthReadOnlyState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 transparent-pass depth state: " + FormatHRESULT(result)).c_str());
        }

    private:
        DX11Device& m_device;
        DX11RenderTargets& m_renderTargets;
        DX11TransparentPassInfo m_info;
        ComPtr<ID3D11DepthStencilState> m_depthReadOnlyState;
    };

    DX11TransparentPass::DX11TransparentPass(DX11Device& device, DX11RenderTargets& renderTargets)
        : m_impl(CreateScope<Impl>(device, renderTargets))
    {
    }

    DX11TransparentPass::~DX11TransparentPass() = default;

    DX11TransparentPass::DX11TransparentPass(DX11TransparentPass&& other) noexcept = default;

    DX11TransparentPass& DX11TransparentPass::operator=(DX11TransparentPass&& other) noexcept = default;

    const DX11TransparentPassInfo& DX11TransparentPass::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    void DX11TransparentPass::Begin(DX11PipelineState& pipelineState)
    {
        m_impl->Begin(pipelineState);
    }

    void DX11TransparentPass::End()
    {
        m_impl->End();
    }
}
