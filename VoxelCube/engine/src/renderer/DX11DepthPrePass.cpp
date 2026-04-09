#include <VoxelCube/Renderer/DX11DepthPrePass.hpp>

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
    class DX11DepthPrePass::Impl
    {
    public:
        Impl(DX11Device& device, DX11RenderTargets& renderTargets)
            : m_device(device)
            , m_renderTargets(renderTargets)
        {
            CreateDepthStencilStates();
            m_info.prePassDepthFunction = "Less";
            m_info.colorPassDepthFunction = "LessEqual";
            m_info.ready = true;

            VC_LOG_INFO(
                "DX11 depth pre-pass created: prePassDepth=" +
                m_info.prePassDepthFunction +
                ", colorPassDepth=" +
                m_info.colorPassDepthFunction +
                ", mode=depth-write/read-only");
        }

        [[nodiscard]] const DX11DepthPrePassInfo& GetInfo() const noexcept
        {
            return m_info;
        }

        void BeginDepthPass(DX11PipelineState& pipelineState)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 depth pre-pass requires an immediate context.");

            m_renderTargets.BindDepthOnly();
            pipelineState.Bind();
            immediateContext->PSSetShader(nullptr, nullptr, 0);
            immediateContext->OMSetDepthStencilState(m_depthWriteState.Get(), 0);
            ++m_info.depthPassCount;
        }

        void BeginColorPass(DX11PipelineState& pipelineState)
        {
            ID3D11DeviceContext* immediateContext = m_device.GetImmediateContext();
            VC_ASSERT(immediateContext != nullptr, "DX11 depth pre-pass requires an immediate context.");

            pipelineState.Bind();
            immediateContext->OMSetDepthStencilState(m_depthReadOnlyState.Get(), 0);
            ++m_info.colorPassCount;
        }

    private:
        void CreateDepthStencilStates()
        {
            ID3D11Device* nativeDevice = m_device.GetNativeDevice();
            VC_ASSERT(nativeDevice != nullptr, "DX11 depth pre-pass requires a ready D3D11 device.");

            D3D11_DEPTH_STENCIL_DESC depthWriteDescription {};
            depthWriteDescription.DepthEnable = TRUE;
            depthWriteDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            depthWriteDescription.DepthFunc = D3D11_COMPARISON_LESS;
            depthWriteDescription.StencilEnable = FALSE;

            HRESULT result = nativeDevice->CreateDepthStencilState(&depthWriteDescription, &m_depthWriteState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 depth pre-pass write state: " + FormatHRESULT(result)).c_str());

            D3D11_DEPTH_STENCIL_DESC depthReadOnlyDescription = depthWriteDescription;
            depthReadOnlyDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depthReadOnlyDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;

            result = nativeDevice->CreateDepthStencilState(&depthReadOnlyDescription, &m_depthReadOnlyState);
            VC_ASSERT(
                SUCCEEDED(result),
                ("Failed to create DX11 depth pre-pass read-only state: " + FormatHRESULT(result)).c_str());
        }

    private:
        DX11Device& m_device;
        DX11RenderTargets& m_renderTargets;
        DX11DepthPrePassInfo m_info;
        ComPtr<ID3D11DepthStencilState> m_depthWriteState;
        ComPtr<ID3D11DepthStencilState> m_depthReadOnlyState;
    };

    DX11DepthPrePass::DX11DepthPrePass(DX11Device& device, DX11RenderTargets& renderTargets)
        : m_impl(CreateScope<Impl>(device, renderTargets))
    {
    }

    DX11DepthPrePass::~DX11DepthPrePass() = default;

    DX11DepthPrePass::DX11DepthPrePass(DX11DepthPrePass&& other) noexcept = default;

    DX11DepthPrePass& DX11DepthPrePass::operator=(DX11DepthPrePass&& other) noexcept = default;

    const DX11DepthPrePassInfo& DX11DepthPrePass::GetInfo() const noexcept
    {
        return m_impl->GetInfo();
    }

    void DX11DepthPrePass::BeginDepthPass(DX11PipelineState& pipelineState)
    {
        m_impl->BeginDepthPass(pipelineState);
    }

    void DX11DepthPrePass::BeginColorPass(DX11PipelineState& pipelineState)
    {
        m_impl->BeginColorPass(pipelineState);
    }
}
