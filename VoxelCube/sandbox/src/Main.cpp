#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <d3d11.h>

#include <VoxelCube/VoxelCube.hpp>

namespace
{
    struct SandboxArenaRecord
    {
        std::uint32_t id = 0;
        std::string label;
        float weight = 0.0f;
    };

    struct SandboxPoolParticle
    {
        std::uint32_t id = 0;
        float lifetime = 0.0f;
        float speed = 0.0f;
    };

    struct SandboxNameComponent
    {
        std::string value;
    };

    struct SandboxHealthComponent
    {
        std::int32_t value = 100;
    };

    struct SandboxVelocityComponent
    {
        vc::Vec3 linear = vc::Vec3::Zero();
    };

    struct SandboxVertex
    {
        float position[3] {};
        float color[4] {};
        float uv[2] {};
    };

    struct SandboxFullscreenVertex
    {
        float position[2] {};
        float uv[2] {};
    };

    using SandboxFloat4x4 = std::array<float, 16>;

    struct SandboxShadowCascadeState
    {
        SandboxFloat4x4 worldToShadowClip {};
        SandboxFloat4x4 worldToShadowTexture {};
        float splitDistance = 1.0f;
    };

    struct SandboxShadowPassBlock
    {
        SandboxFloat4x4 worldToShadowClip {};
    };

    struct SandboxAtlasTriangleVariant
    {
        std::string regionName;
        std::string uvSummary;
        vc::Scope<vc::DX11GeometryBuffer> geometry;
        vc::Scope<vc::DX11GeometryBuffer> transparentGeometry;
    };

    struct SandboxGpuFrameBlock
    {
        std::uint32_t frameIndex = 0;
        std::uint32_t clearCount = 0;
        std::uint32_t aspectRatioMilli = 0;
        std::uint32_t checksum = 0;
    };

    static_assert(sizeof(SandboxGpuFrameBlock) == 16, "SandboxGpuFrameBlock must stay 16 bytes.");
    static_assert(sizeof(SandboxShadowPassBlock) == 64, "SandboxShadowPassBlock must stay 64 bytes.");

    [[nodiscard]] SandboxFloat4x4 MultiplyMatrices(const SandboxFloat4x4& left, const SandboxFloat4x4& right)
    {
        SandboxFloat4x4 result {};
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
            {
                float value = 0.0f;
                for (std::size_t index = 0; index < 4; ++index)
                {
                    value += left[row * 4 + index] * right[index * 4 + column];
                }

                result[row * 4 + column] = value;
            }
        }

        return result;
    }

    [[nodiscard]] SandboxFloat4x4 CreateLookAtMatrix(
        const vc::Vec3& eye,
        const vc::Vec3& target,
        const vc::Vec3& upDirection)
    {
        vc::Vec3 forward = (eye - target).Normalized();
        vc::Vec3 right = vc::Vec3::Cross(upDirection, forward).Normalized();
        vc::Vec3 up = vc::Vec3::Cross(forward, right).Normalized();
        if (right.IsNearlyZero() || up.IsNearlyZero())
        {
            right = vc::Vec3::Right();
            up = vc::Vec3::Up();
            forward = vc::Vec3::Forward();
        }

        return {
            right.x, up.x, forward.x, 0.0f,
            right.y, up.y, forward.y, 0.0f,
            right.z, up.z, forward.z, 0.0f,
            -vc::Vec3::Dot(right, eye),
            -vc::Vec3::Dot(up, eye),
            -vc::Vec3::Dot(forward, eye),
            1.0f
        };
    }

    [[nodiscard]] SandboxFloat4x4 CreateOrthographicMatrix(float width, float height, float nearClip, float farClip)
    {
        const float safeWidth = width > 1.0e-4f ? width : 1.0f;
        const float safeHeight = height > 1.0e-4f ? height : 1.0f;
        const float safeFarClip = farClip > nearClip + 1.0e-4f ? farClip : nearClip + 1.0f;
        const float inverseDepth = 1.0f / (nearClip - safeFarClip);

        return {
            2.0f / safeWidth, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f / safeHeight, 0.0f, 0.0f,
            0.0f, 0.0f, inverseDepth, 0.0f,
            0.0f, 0.0f, nearClip * inverseDepth, 1.0f
        };
    }

    [[nodiscard]] SandboxFloat4x4 CreateShadowTextureTransform()
    {
        return {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f
        };
    }

    [[nodiscard]] vc::DX11DirectionalLightParameters BuildSandboxDirectionalLightParameters(std::uint64_t frameIndex)
    {
        const float time = static_cast<float>(frameIndex) * 0.01f;
        vc::DX11DirectionalLightParameters parameters {};
        parameters.direction = {
            0.34f + 0.08f * std::sin(time * 0.37f + 0.2f),
            -0.82f,
            -0.52f + 0.06f * std::cos(time * 0.21f + 0.5f)
        };
        parameters.intensity = 1.15f + 0.12f * std::sin(time * 0.31f + 0.4f);
        parameters.color = { 1.00f, 0.94f, 0.85f };
        parameters.ambientIntensity = 0.22f + 0.04f * std::cos(time * 0.19f + 0.1f);
        return parameters;
    }

    [[nodiscard]] std::array<SandboxShadowCascadeState, vc::DX11ShadowCascadeCount> BuildSandboxShadowCascadeStates(
        const vc::Vec3& lightDirection)
    {
        constexpr std::array<float, vc::DX11ShadowCascadeCount> kHalfExtents { 0.95f, 1.80f };
        constexpr std::array<float, vc::DX11ShadowCascadeCount> kSplitDistances { 0.45f, 1.10f };

        const vc::Vec3 sceneCenter(0.0f, -0.18f, 0.38f);
        const vc::Vec3 normalizedDirection = lightDirection.IsNearlyZero()
            ? vc::Vec3(0.34f, -0.82f, -0.52f).Normalized()
            : lightDirection.Normalized();
        const vc::Vec3 upDirection = std::fabs(vc::Vec3::Dot(normalizedDirection, vc::Vec3::Up())) > 0.95f
            ? vc::Vec3::Forward()
            : vc::Vec3::Up();
        const SandboxFloat4x4 textureTransform = CreateShadowTextureTransform();

        std::array<SandboxShadowCascadeState, vc::DX11ShadowCascadeCount> cascadeStates {};
        for (std::size_t cascadeIndex = 0; cascadeIndex < cascadeStates.size(); ++cascadeIndex)
        {
            const float halfExtent = kHalfExtents[cascadeIndex];
            const float depthRange = halfExtent * 4.0f;
            const vc::Vec3 eye = sceneCenter - normalizedDirection * (halfExtent * 2.25f);
            const SandboxFloat4x4 view = CreateLookAtMatrix(eye, sceneCenter, upDirection);
            const SandboxFloat4x4 projection = CreateOrthographicMatrix(
                halfExtent * 2.0f,
                halfExtent * 2.0f,
                0.1f,
                depthRange);
            cascadeStates[cascadeIndex].worldToShadowClip = MultiplyMatrices(view, projection);
            cascadeStates[cascadeIndex].worldToShadowTexture = MultiplyMatrices(
                cascadeStates[cascadeIndex].worldToShadowClip,
                textureTransform);
            cascadeStates[cascadeIndex].splitDistance = kSplitDistances[cascadeIndex];
        }

        return cascadeStates;
    }

    [[nodiscard]] std::uint32_t QuantizeAspectRatio(float aspectRatio)
    {
        const float safeAspectRatio = aspectRatio > 0.0f ? aspectRatio : 0.0f;
        return static_cast<std::uint32_t>(std::round(safeAspectRatio * 1000.0f));
    }

    [[nodiscard]] std::uint32_t ComputeGpuFrameChecksum(const SandboxGpuFrameBlock& block)
    {
        return block.frameIndex ^ block.clearCount ^ block.aspectRatioMilli ^ 0x0A11CE55u;
    }

    [[nodiscard]] vc::Path ResolveSandboxShaderPath()
    {
        const std::array<vc::Path, 4> candidates {
            vc::Path("shaders/sandbox_bootstrap.hlsl"),
            vc::Path("../shaders/sandbox_bootstrap.hlsl"),
            vc::Path("../../shaders/sandbox_bootstrap.hlsl"),
            vc::Path("../../../shaders/sandbox_bootstrap.hlsl")
        };

        for (const vc::Path& candidate : candidates)
        {
            if (vc::FileSystem::IsFile(candidate))
            {
                return vc::FileSystem::Normalize(candidate);
            }
        }

        VC_ASSERT(false, "Failed to resolve sandbox HLSL shader path.");
        return {};
    }

    [[nodiscard]] vc::Path ResolveSandboxProjectRoot()
    {
        const vc::Path shaderPath = ResolveSandboxShaderPath();
        const vc::Path projectRoot = shaderPath.parent_path().parent_path();
        VC_ASSERT(!projectRoot.empty(), "Failed to resolve sandbox project root.");
        return vc::FileSystem::Normalize(projectRoot);
    }

    [[nodiscard]] vc::Path ResolveSandboxTexturePath()
    {
        return vc::FileSystem::Normalize(ResolveSandboxProjectRoot() / "sandbox/assets/bootstrap_checker.dds");
    }

    [[nodiscard]] std::vector<std::uint8_t> BuildSandboxBootstrapTextureDds()
    {
        constexpr std::uint32_t width = 2;
        constexpr std::uint32_t height = 2;
        constexpr std::uint32_t bytesPerPixel = 4;
        constexpr std::uint32_t ddsHeaderFlags = 0x0000100Fu;
        constexpr std::uint32_t ddsCapsTexture = 0x00001000u;
        constexpr std::uint32_t ddsPixelFormatFlags = 0x00000041u;

        std::vector<std::uint8_t> bytes;
        bytes.reserve(4 + 124 + width * height * bytesPerPixel);

        const auto appendU32 = [&bytes](std::uint32_t value)
        {
            bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
            bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
            bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
        };

        appendU32(0x20534444u);
        appendU32(124u);
        appendU32(ddsHeaderFlags);
        appendU32(height);
        appendU32(width);
        appendU32(width * bytesPerPixel);
        appendU32(0u);
        appendU32(1u);
        for (std::uint32_t reservedIndex = 0; reservedIndex < 11u; ++reservedIndex)
        {
            appendU32(0u);
        }

        appendU32(32u);
        appendU32(ddsPixelFormatFlags);
        appendU32(0u);
        appendU32(32u);
        appendU32(0x00FF0000u);
        appendU32(0x0000FF00u);
        appendU32(0x000000FFu);
        appendU32(0xFF000000u);

        appendU32(ddsCapsTexture);
        appendU32(0u);
        appendU32(0u);
        appendU32(0u);
        appendU32(0u);

        const std::array<std::uint8_t, 16> pixels {
            0x30u, 0x68u, 0xF4u, 0xFFu,
            0xF0u, 0xC0u, 0x30u, 0xFFu,
            0x64u, 0xE8u, 0x48u, 0xFFu,
            0x38u, 0x18u, 0x12u, 0xFFu
        };
        bytes.insert(bytes.end(), pixels.begin(), pixels.end());
        return bytes;
    }

    [[nodiscard]] bool EnsureSandboxTextureAsset(const vc::Path& path)
    {
        if (vc::FileSystem::IsFile(path))
        {
            return false;
        }

        const vc::Path parentPath = path.parent_path();
        if (!parentPath.empty())
        {
            std::error_code errorCode;
            std::filesystem::create_directories(parentPath, errorCode);
            if (errorCode)
            {
                throw std::filesystem::filesystem_error("Failed to create sandbox texture directory.", parentPath, errorCode);
            }
        }

        const std::vector<std::uint8_t> bytes = BuildSandboxBootstrapTextureDds();
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error("Failed to open sandbox DDS file for writing: " + path.string());
        }

        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output.good())
        {
            throw std::runtime_error("Failed to write sandbox DDS file: " + path.string());
        }

        return true;
    }

    [[nodiscard]] std::string FormatVec3(const vc::Vec3& value)
    {
        return "(" +
            std::to_string(value.x) +
            ", " +
            std::to_string(value.y) +
            ", " +
            std::to_string(value.z) +
            ")";
    }

    [[nodiscard]] std::string FormatUvRect(float uMin, float vMin, float uMax, float vMax)
    {
        return "(" +
            std::to_string(uMin) +
            ", " +
            std::to_string(vMin) +
            ") -> (" +
            std::to_string(uMax) +
            ", " +
            std::to_string(vMax) +
            ")";
    }

    [[nodiscard]] std::string JoinLabels(const std::vector<std::string_view>& labels)
    {
        if (labels.empty())
        {
            return "none";
        }

        std::string result;
        for (std::size_t index = 0; index < labels.size(); ++index)
        {
            if (index > 0)
            {
                result += " -> ";
            }

            result += labels[index];
        }

        return result;
    }

    [[nodiscard]] bool IsPrime(std::uint32_t value)
    {
        if (value < 2)
        {
            return false;
        }

        for (std::uint32_t divisor = 2; divisor * divisor <= value; ++divisor)
        {
            if (value % divisor == 0)
            {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::uint32_t CountPrimes(std::uint32_t limit)
    {
        std::uint32_t count = 0;
        for (std::uint32_t value = 2; value <= limit; ++value)
        {
            if (IsPrime(value))
            {
                ++count;
            }
        }

        return count;
    }

    vc::ApplicationSpecification GetDefaultSandboxSpecification()
    {
        return {
            .name = "VoxelCube Sandbox",
            .windowWidth = 1600,
            .windowHeight = 900,
            .windowResizable = true,
            .startMaximized = false,
            .startFullscreen = false,
            .workerThreadCount = 0,
            .fixedTimeStep = 1.0 / 60.0,
            .targetFrameRate = 60,
            .maxDeltaTime = 0.25,
            .maxFrames = 300
        };
    }

    vc::ApplicationSpecification LoadSandboxSpecification()
    {
        const vc::ApplicationSpecification defaults = GetDefaultSandboxSpecification();

        try
        {
            return vc::LoadApplicationSpecificationFromFile("sandbox/sandbox.vcconfig", defaults);
        }
        catch (const std::exception& exception)
        {
            std::cerr << "Failed to load sandbox config: " << exception.what() << '\n';
            return defaults;
        }
    }
}

class SandboxApplication final : public vc::Application
{
public:
    SandboxApplication()
        : vc::Application(LoadSandboxSpecification())
    {
    }

protected:
    void OnInitialize() override
    {
        m_configWatcher = vc::CreateScope<vc::FileSystemWatcher>("sandbox/sandbox.vcconfig", false);
        GetEventBus().Subscribe<vc::WindowResizeEvent>(
            [](vc::WindowResizeEvent& event)
            {
                VC_LOG_INFO("Window resized to " + std::to_string(event.width) + "x" + std::to_string(event.height));
            });
        GetEventBus().Subscribe<vc::MouseScrolledEvent>(
            [](vc::MouseScrolledEvent& event)
            {
                VC_LOG_INFO("Mouse wheel delta: " + std::to_string(event.delta));
            });
        GetEventBus().Subscribe<vc::WindowCloseEvent>(
            [](vc::WindowCloseEvent&)
            {
                VC_LOG_INFO("EventBus observed window close request.");
            });

        m_dx11Device = vc::CreateScope<vc::DX11Device>();
        const vc::DX11DeviceInfo& dx11Info = m_dx11Device->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 bootstrap: adapter=" +
            dx11Info.adapter.description +
            ", featureLevel=" +
            dx11Info.featureLevel +
            ", context=" +
            dx11Info.immediateContext.type +
            ", contextFlags=" +
            std::to_string(dx11Info.immediateContext.flags) +
            ", contextAvailable=" +
            std::string(dx11Info.immediateContext.available ? "true" : "false") +
            ", debugLayer=" +
            std::string(dx11Info.debugLayerEnabled ? "true" : "false") +
            ", driver=" +
            std::string(dx11Info.warpDriver ? "WARP" : "Hardware") +
            ", vendorId=" +
            std::to_string(dx11Info.adapter.vendorId));
        VC_LOG_INFO(
            "Sandbox DX11 native handles: device=" +
            std::string(m_dx11Device->GetNativeDevice() != nullptr ? "ready" : "missing") +
            ", immediateContext=" +
            std::string(m_dx11Device->HasImmediateContext() && m_dx11Device->GetImmediateContext() != nullptr ? "ready" : "missing"));
        m_dx11ContextSync = vc::CreateScope<vc::DX11ContextSync>(*m_dx11Device);
        const vc::DX11ContextSyncInfo& contextSyncInfo = m_dx11ContextSync->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 context sync: queryReady=" +
            std::string(contextSyncInfo.queryReady ? "true" : "false") +
            ", flushCount=" +
            std::to_string(contextSyncInfo.flushCount) +
            ", waitCount=" +
            std::to_string(contextSyncInfo.waitCount));
        m_dx11DynamicBuffer = vc::CreateScope<vc::DX11Buffer>(
            *m_dx11Device,
            vc::DX11BufferSpecification {
                .sizeInBytes = sizeof(SandboxGpuFrameBlock),
                .stride = sizeof(SandboxGpuFrameBlock),
                .kind = vc::DX11BufferKind::Constant,
                .usage = vc::DX11BufferUsage::Dynamic,
                .cpuReadable = false,
                .cpuWritable = true
            });
        m_dx11ReadbackBuffer = vc::CreateScope<vc::DX11Buffer>(
            *m_dx11Device,
            vc::DX11BufferSpecification {
                .sizeInBytes = sizeof(SandboxGpuFrameBlock),
                .stride = sizeof(SandboxGpuFrameBlock),
                .kind = vc::DX11BufferKind::Generic,
                .usage = vc::DX11BufferUsage::Staging,
                .cpuReadable = true,
                .cpuWritable = false
            });
        const vc::DX11BufferInfo& dynamicBufferInfo = m_dx11DynamicBuffer->GetInfo();
        const vc::DX11BufferInfo& readbackBufferInfo = m_dx11ReadbackBuffer->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 buffers: upload=" +
            dynamicBufferInfo.kind +
            "/" +
            dynamicBufferInfo.usage +
            ", readback=" +
            readbackBufferInfo.kind +
            "/" +
            readbackBufferInfo.usage +
            ", bytes=" +
            std::to_string(dynamicBufferInfo.sizeInBytes));
        RunDx11ShaderCompilerSmokeTest();
        m_dx11SwapChain = vc::CreateScope<vc::DX11SwapChain>(
            *m_dx11Device,
            vc::DX11SwapChainSpecification {
                .windowHandle = GetWindow().GetNativeHandle(),
                .width = GetWindow().GetWidth(),
                .height = GetWindow().GetHeight(),
                .bufferCount = 2,
                .enableVSync = true
            });
        const vc::DX11SwapChainInfo& swapChainInfo = m_dx11SwapChain->GetInfo();
        VC_LOG_INFO(
            "Sandbox DXGI swap chain: " +
            std::to_string(swapChainInfo.width) +
            "x" +
            std::to_string(swapChainInfo.height) +
            ", buffers=" +
            std::to_string(swapChainInfo.bufferCount) +
            ", format=" +
            swapChainInfo.format +
            ", effect=" +
            swapChainInfo.swapEffect +
            ", vsync=" +
            std::string(swapChainInfo.vSyncEnabled ? "true" : "false"));
        m_dx11RenderTargets = vc::CreateScope<vc::DX11RenderTargets>(*m_dx11Device, *m_dx11SwapChain);
        const vc::DX11RenderTargetsInfo& renderTargetsInfo = m_dx11RenderTargets->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 render targets: color=" +
            renderTargetsInfo.colorFormat +
            ", depth=" +
            renderTargetsInfo.depthStencilFormat +
            ", depthSRV=" +
            renderTargetsInfo.depthShaderResourceFormat +
            ", size=" +
            std::to_string(renderTargetsInfo.width) +
            "x" +
            std::to_string(renderTargetsInfo.height) +
            ", ready=" +
            std::string(renderTargetsInfo.ready ? "true" : "false"));
        InitializeDx11ShadowMap();
        m_dx11GBuffer = vc::CreateScope<vc::DX11GBuffer>(*m_dx11Device, *m_dx11RenderTargets);
        const vc::DX11GBufferInfo& gBufferInfo = m_dx11GBuffer->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 G-buffer: targets=" +
            std::to_string(gBufferInfo.renderTargetCount) +
            ", albedo=" +
            gBufferInfo.albedoFormat +
            ", normal=" +
            gBufferInfo.normalFormat +
            ", material=" +
            gBufferInfo.materialFormat +
            ", size=" +
            std::to_string(gBufferInfo.width) +
            "x" +
            std::to_string(gBufferInfo.height));
        m_dx11DepthPrePass = vc::CreateScope<vc::DX11DepthPrePass>(*m_dx11Device, *m_dx11RenderTargets);
        const vc::DX11DepthPrePassInfo& depthPrePassInfo = m_dx11DepthPrePass->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 depth pre-pass: ready=" +
            std::string(depthPrePassInfo.ready ? "true" : "false") +
            ", prePassDepth=" +
            depthPrePassInfo.prePassDepthFunction +
            ", colorPassDepth=" +
            depthPrePassInfo.colorPassDepthFunction);
        InitializeDx11ShadowPipelineState();
        InitializeDx11ShadowPassBuffer();
        InitializeDx11PipelineState();
        InitializeDx11BootstrapTexture();
        InitializeDx11TextureAtlas();
        InitializeDx11Geometry();
        InitializeDx11LightingPipelineState();
        InitializeDx11LightingGeometry();
        InitializeDx11SsaoPipelineState();
        InitializeDx11SsaoPass();
        InitializeDx11LightingPass();
        InitializeDx11TransparentPipelineState();
        InitializeDx11TransparentPass();

        VC_LOG_INFO("ECS backend: " + std::string(m_registry.GetBackendName()));

        vc::Entity playerEntity = m_registry.CreateEntity();
        vc::Entity sunEntity = m_registry.CreateEntity();

        playerEntity.AddComponent<SandboxNameComponent>(SandboxNameComponent { .value = "player" });
        playerEntity.AddComponent<vc::TransformComponent>(vc::TransformComponent::FromTRS(
            vc::Vec3(8.0f, 24.0f, -3.0f),
            vc::Vec3(5.0f, 35.0f, 0.0f),
            vc::Vec3::One()));
        playerEntity.AddComponent<SandboxHealthComponent>(SandboxHealthComponent { .value = 100 });

        sunEntity.AddComponent<SandboxNameComponent>(SandboxNameComponent { .value = "sun-anchor" });
        sunEntity.AddComponent<vc::TransformComponent>(vc::TransformComponent::FromTRS(
            vc::Vec3(64.0f, 96.0f, 12.0f),
            vc::Vec3(-20.0f, 0.0f, 0.0f),
            vc::Vec3(4.0f)));

        SandboxHealthComponent& playerHealth = playerEntity.GetComponent<SandboxHealthComponent>();
        playerHealth.value -= 15;

        const vc::TransformComponent& playerTransform = playerEntity.GetComponent<vc::TransformComponent>();
        VC_LOG_INFO(
            "Player transform bootstrap: position=" +
            FormatVec3(playerTransform.translation) +
            ", forward=" +
            FormatVec3(playerTransform.GetForwardDirection()) +
            ", uniform scale=" +
            std::string(playerTransform.HasUniformScale() ? "true" : "false"));

        std::size_t ecsViewCount = 0;
        m_registry.View<SandboxNameComponent, vc::TransformComponent>(
            [&ecsViewCount](vc::Entity entity, SandboxNameComponent& name, vc::TransformComponent& transform)
            {
                ++ecsViewCount;
                VC_LOG_INFO(
                    "ECS view entity #" +
                    std::to_string(entity.GetId()) +
                    " [" +
                    name.value +
                    "] at (" +
                    std::to_string(transform.translation.x) +
                    ", " +
                    std::to_string(transform.translation.y) +
                    ", " +
                    std::to_string(transform.translation.z) +
                    "), scale=" +
                    FormatVec3(transform.scale));
            });

        sunEntity.RemoveComponent<vc::TransformComponent>();
        VC_LOG_INFO(
            "Spawned ECS entities: alive=" +
            std::to_string(m_registry.GetAliveCount()) +
            ", view matches=" +
            std::to_string(ecsViewCount) +
            ", player health=" +
            std::to_string(playerHealth.value));
        VC_LOG_INFO(
            "Removed Transform from entity #" +
            std::to_string(sunEntity.GetId()) +
            ", has transform=" +
            std::string(sunEntity.HasComponent<vc::TransformComponent>() ? "true" : "false"));

        sunEntity.Destroy();
        VC_LOG_INFO("Destroyed temporary ECS entity, alive=" + std::to_string(m_registry.GetAliveCount()));

        m_playerEntityId = playerEntity.GetId();
        vc::Entity cameraEntity = m_registry.CreateEntity();
        cameraEntity.AddComponent<SandboxNameComponent>(SandboxNameComponent { .value = "main-camera" });
        cameraEntity.AddComponent<vc::TransformComponent>(vc::TransformComponent::FromTRS(
            playerTransform.translation - playerTransform.GetForwardDirection() * 9.0f + vc::Vec3(0.0f, 3.25f, 0.0f),
            vc::Vec3(18.0f, playerTransform.rotationEulerDegrees.y, 0.0f),
            vc::Vec3::One()));
        cameraEntity.AddComponent<vc::CameraComponent>(vc::CameraComponent::Perspective(
            60.0f,
            GetWindowAspectRatio(),
            0.1f,
            2000.0f,
            true));
        m_cameraEntityId = cameraEntity.GetId();

        const vc::CameraComponent& camera = cameraEntity.GetComponent<vc::CameraComponent>();
        const vc::TransformComponent& cameraTransform = cameraEntity.GetComponent<vc::TransformComponent>();
        VC_LOG_INFO(
            "Camera bootstrap: type=" +
            std::string(vc::ToString(camera.projectionType)) +
            ", primary=" +
            std::string(camera.primary ? "true" : "false") +
            ", aspect=" +
            std::to_string(camera.aspectRatio) +
            ", fovY=" +
            std::to_string(camera.verticalFovDegrees) +
            ", fovX=" +
            std::to_string(camera.GetHorizontalFovDegrees()) +
            ", pos=" +
            FormatVec3(cameraTransform.translation));

        RegisterSandboxSystems();
        LogSystemOrder(vc::SystemStage::Startup);
        LogSystemOrder(vc::SystemStage::Update);
        LogSystemOrder(vc::SystemStage::FixedUpdate);
        LogSystemOrder(vc::SystemStage::Shutdown);
        m_systemScheduler.RunStage(vc::SystemStage::Startup, MakeSystemContext());

        std::atomic<std::uint64_t> warmupChecksum = 0;
        vc::JobSystem::ParallelFor(1024, [&warmupChecksum](std::size_t index)
        {
            const std::uint64_t value = static_cast<std::uint64_t>(index + 1);
            warmupChecksum.fetch_add(value * value, std::memory_order_relaxed);
        });

        VC_LOG_INFO("JobSystem workers: " + std::to_string(vc::JobSystem::GetWorkerCount()));
        VC_LOG_INFO("ParallelFor warmup checksum: " + std::to_string(warmupChecksum.load(std::memory_order_relaxed)));

        m_bootstrapArena = vc::CreateScope<vc::ArenaAllocator>(16 * 1024);
        SandboxArenaRecord* bootstrapRecordA = m_bootstrapArena->Create<SandboxArenaRecord>(SandboxArenaRecord {
            .id = 1,
            .label = "terrain-bootstrap",
            .weight = 4.5f
        });
        SandboxArenaRecord* bootstrapRecordB = m_bootstrapArena->Create<SandboxArenaRecord>(SandboxArenaRecord {
            .id = 2,
            .label = "lighting-bootstrap",
            .weight = 2.25f
        });
        VC_LOG_INFO(
            "Arena sample records: #" +
            std::to_string(bootstrapRecordA->id) +
            " " +
            bootstrapRecordA->label +
            ", #" +
            std::to_string(bootstrapRecordB->id) +
            " " +
            bootstrapRecordB->label);
        VC_LOG_INFO(
            "Arena usage before reset: " +
            std::to_string(m_bootstrapArena->GetUsed()) +
            "/" +
            std::to_string(m_bootstrapArena->GetCapacity()) +
            " bytes");
        m_bootstrapArena->Reset();
        VC_LOG_INFO("Arena reset complete, used bytes=" + std::to_string(m_bootstrapArena->GetUsed()));

        m_particlePool = vc::CreateScope<vc::PoolAllocator>(sizeof(SandboxPoolParticle), 32, alignof(SandboxPoolParticle));
        m_retainedParticle = m_particlePool->Create<SandboxPoolParticle>(SandboxPoolParticle {
            .id = 7,
            .lifetime = 3.0f,
            .speed = 12.5f
        });
        SandboxPoolParticle* temporaryParticle = m_particlePool->Create<SandboxPoolParticle>(SandboxPoolParticle {
            .id = 8,
            .lifetime = 1.5f,
            .speed = 18.0f
        });
        VC_LOG_INFO(
            "Pool usage after allocations: used=" +
            std::to_string(m_particlePool->GetUsedCount()) +
            ", free=" +
            std::to_string(m_particlePool->GetFreeCount()));
        m_particlePool->Destroy(temporaryParticle);
        VC_LOG_INFO(
            "Pool usage after releasing temporary particle: used=" +
            std::to_string(m_particlePool->GetUsedCount()) +
            ", free=" +
            std::to_string(m_particlePool->GetFreeCount()));

        const vc::MemoryStatistics initialMemoryStats = vc::Memory::GetStatistics();
        VC_LOG_INFO(
            "Memory stats: active=" +
            std::to_string(initialMemoryStats.activeBytes) +
            " bytes, peak=" +
            std::to_string(initialMemoryStats.peakActiveBytes) +
            " bytes, allocations=" +
            std::to_string(initialMemoryStats.allocationCount));

        m_primeJob = vc::JobSystem::Enqueue([this]()
        {
            m_primeCountResult = CountPrimes(100000);
        });
        VC_LOG_INFO("Queued background prime-count job #" + std::to_string(m_primeJob.GetId()));

        VC_LOG_INFO("Sandbox bootstrap finished.");
        VC_LOG_INFO("Config source: sandbox/sandbox.vcconfig");
        VC_LOG_INFO("Controls: resize the window, press F11 or Alt+Enter to toggle fullscreen.");
    }

    void OnUpdate(vc::Timestep deltaTime) override
    {
        const auto nextFrame = GetFrameCount() + 1;

        if (m_configWatcher)
        {
            for (const vc::FileSystemChange& change : m_configWatcher->PollChanges())
            {
                std::string changeType = "modified";
                if (change.type == vc::FileSystemChangeType::Added)
                {
                    changeType = "added";
                }
                else if (change.type == vc::FileSystemChangeType::Removed)
                {
                    changeType = "removed";
                }

                VC_LOG_INFO("Detected sandbox config change: " + changeType + " -> " + change.path.string());
            }
        }

        if (vc::Input::WasKeyPressed(vc::KeyCode::Escape))
        {
            VC_LOG_INFO("Escape pressed, closing sandbox.");
            Close();
            return;
        }

        if (!m_primeJobReported && m_primeJob.IsValid() && m_primeJob.IsCompleted())
        {
            m_primeJob.Wait();
            m_primeJobReported = true;
            VC_LOG_INFO(
                "Background job finished: primes up to 100000 = " +
                std::to_string(m_primeCountResult) +
                ", queued jobs=" +
                std::to_string(vc::JobSystem::GetQueuedJobCount()) +
                ", active jobs=" +
                std::to_string(vc::JobSystem::GetActiveJobCount()));
        }

        m_systemScheduler.RunStage(vc::SystemStage::Update, MakeSystemContext(deltaTime, nextFrame, GetFixedTickCount()));

        if (vc::Input::WasMouseButtonPressed(vc::MouseButton::Left))
        {
            VC_LOG_INFO(
                "Left mouse button pressed at " +
                std::to_string(vc::Input::GetMouseX()) +
                "x" +
                std::to_string(vc::Input::GetMouseY()));
        }

        if (vc::Input::HasRawMouseDelta() && nextFrame % 60 == 0)
        {
            VC_LOG_TRACE(
                "Raw mouse delta: " +
                std::to_string(vc::Input::GetMouseDeltaX()) +
                ", " +
                std::to_string(vc::Input::GetMouseDeltaY()));
        }

        if (nextFrame % 60 == 0)
        {
            VC_LOG_TRACE("Frame " + std::to_string(nextFrame) + " dt=" + std::to_string(deltaTime.GetMilliseconds()) + "ms");
        }

        UpdateActiveDx11AtlasVariant(nextFrame);

        if (m_dx11RenderTargets && m_dx11SwapChain && !GetWindow().IsMinimized())
        {
            const float time = static_cast<float>(nextFrame) * 0.01f;
            const std::array<float, 4> clearColor {
                0.12f + 0.08f * std::sin(time),
                0.18f + 0.06f * std::cos(time * 0.7f),
                0.24f + 0.08f * std::sin(time * 0.5f + 0.6f),
                1.0f
            };
            m_dx11RenderTargets->Clear(clearColor);
            const SandboxAtlasTriangleVariant* activeVariant = GetActiveDx11AtlasVariant();

            if (m_dx11DepthPrePass
                && m_dx11GBuffer
                && m_dx11SsaoPass
                && m_dx11SsaoPipelineState
                && m_dx11PipelineState
                && m_dx11LightingPass
                && m_dx11LightingPipelineState
                && m_dx11LightingGeometry
                && activeVariant != nullptr)
            {
                if (m_dx11ShadowMap && m_dx11ShadowPipelineState && m_dx11ShadowPassBuffer && m_dx11Device)
                {
                    UpdateDx11ShadowPassData(nextFrame);
                    m_dx11ShadowMap->Clear();

                    ID3D11DeviceContext* immediateContext = m_dx11Device->GetImmediateContext();
                    VC_ASSERT(immediateContext != nullptr, "DX11 shadow pass requires an immediate context.");

                    for (std::uint32_t cascadeIndex = 0;
                         cascadeIndex < m_dx11ShadowMap->GetInfo().cascadeCount && cascadeIndex < m_shadowCascadeStates.size();
                         ++cascadeIndex)
                    {
                        SandboxShadowPassBlock shadowPassBlock {};
                        shadowPassBlock.worldToShadowClip = m_shadowCascadeStates[cascadeIndex].worldToShadowClip;
                        m_dx11ShadowPassBuffer->Write(&shadowPassBlock, sizeof(shadowPassBlock));

                        m_dx11ShadowMap->BindCascade(cascadeIndex);
                        m_dx11ShadowPipelineState->Bind();

                        ID3D11Buffer* shadowConstantBuffer = m_dx11ShadowPassBuffer->GetNativeBuffer();
                        VC_ASSERT(shadowConstantBuffer != nullptr, "DX11 shadow pass constant buffer is not ready.");
                        immediateContext->VSSetConstantBuffers(0, 1, &shadowConstantBuffer);

                        activeVariant->geometry->Bind();
                        activeVariant->geometry->DrawIndexed();
                    }

                    ID3D11Buffer* nullShadowConstantBuffer = nullptr;
                    immediateContext->VSSetConstantBuffers(0, 1, &nullShadowConstantBuffer);
                }

                m_dx11DepthPrePass->BeginDepthPass(*m_dx11PipelineState);
                activeVariant->geometry->Bind();
                activeVariant->geometry->DrawIndexed();

                static constexpr std::array<float, 4> kGBufferAlbedoClear { 0.0f, 0.0f, 0.0f, 0.0f };
                static constexpr std::array<float, 4> kGBufferNormalClear { 0.5f, 0.5f, 1.0f, 0.0f };
                static constexpr std::array<float, 4> kGBufferMaterialClear { 0.0f, 0.0f, 0.0f, 0.0f };
                m_dx11GBuffer->Clear(kGBufferAlbedoClear, kGBufferNormalClear, kGBufferMaterialClear);
                m_dx11GBuffer->Bind();
                m_dx11DepthPrePass->BeginColorPass(*m_dx11PipelineState);
                if (m_dx11TextureAtlas)
                {
                    m_dx11TextureAtlas->BindPS(0);
                }
                activeVariant->geometry->Bind();
                activeVariant->geometry->DrawIndexed();

                UpdateDx11SsaoParameters(nextFrame);
                m_dx11SsaoPass->Clear();
                m_dx11SsaoPass->Begin(*m_dx11SsaoPipelineState);
                m_dx11LightingGeometry->Bind();
                m_dx11LightingGeometry->DrawIndexed();
                m_dx11SsaoPass->End();

                UpdateDx11LightingPassParameters(nextFrame);
                m_dx11LightingPass->Begin(*m_dx11LightingPipelineState);
                m_dx11LightingGeometry->Bind();
                m_dx11LightingGeometry->DrawIndexed();
                m_dx11LightingPass->End();

                if (m_dx11TransparentPass
                    && m_dx11TransparentPipelineState
                    && m_dx11TextureAtlas
                    && activeVariant->transparentGeometry)
                {
                    m_dx11TransparentPass->Begin(*m_dx11TransparentPipelineState);
                    m_dx11TextureAtlas->BindPS(0);
                    activeVariant->transparentGeometry->Bind();
                    activeVariant->transparentGeometry->DrawIndexed();
                    m_dx11TransparentPass->End();
                }
            }
            else
            {
                m_dx11RenderTargets->Bind();
                if (m_dx11PipelineState)
                {
                    m_dx11PipelineState->Bind();
                }
                if (m_dx11TextureAtlas)
                {
                    m_dx11TextureAtlas->BindPS(0);
                }
                if (activeVariant != nullptr)
                {
                    activeVariant->geometry->Bind();
                    activeVariant->geometry->DrawIndexed();
                }
            }

            const bool presented = m_dx11SwapChain->Present();
            if (presented && nextFrame % 120 == 0)
            {
                RefreshDx11BufferReadback(
                    static_cast<std::uint32_t>(nextFrame),
                    static_cast<std::uint32_t>(m_dx11RenderTargets->GetInfo().clearCount));

                VC_LOG_INFO(
                    "Swap chain present count=" +
                    std::to_string(m_dx11SwapChain->GetInfo().presentCount) +
                    ", clear count=" +
                    std::to_string(m_dx11RenderTargets->GetInfo().clearCount) +
                    ", flush count=" +
                    std::to_string(m_dx11ContextSync ? m_dx11ContextSync->GetInfo().flushCount : 0) +
                    ", buffer writes=" +
                    std::to_string(m_dx11DynamicBuffer ? m_dx11DynamicBuffer->GetInfo().writeCount : 0) +
                    ", readback copies=" +
                    std::to_string(m_dx11ReadbackBuffer ? m_dx11ReadbackBuffer->GetInfo().copyCount : 0) +
                    ", depth passes=" +
                    std::to_string(m_dx11DepthPrePass ? m_dx11DepthPrePass->GetInfo().depthPassCount : 0) +
                    ", color passes=" +
                    std::to_string(m_dx11DepthPrePass ? m_dx11DepthPrePass->GetInfo().colorPassCount : 0) +
                    ", gbuffer binds=" +
                    std::to_string(m_dx11GBuffer ? m_dx11GBuffer->GetInfo().bindCount : 0) +
                    ", gbuffer clears=" +
                    std::to_string(m_dx11GBuffer ? m_dx11GBuffer->GetInfo().clearCount : 0) +
                    ", ssao passes=" +
                    std::to_string(m_dx11SsaoPass ? m_dx11SsaoPass->GetInfo().passCount : 0) +
                    ", ssao clears=" +
                    std::to_string(m_dx11SsaoPass ? m_dx11SsaoPass->GetInfo().clearCount : 0) +
                    ", ssao updates=" +
                    std::to_string(m_dx11SsaoPass ? m_dx11SsaoPass->GetInfo().parameterUpdateCount : 0) +
                    ", shadow clears=" +
                    std::to_string(m_dx11ShadowMap ? m_dx11ShadowMap->GetInfo().clearCount : 0) +
                    ", shadow passes=" +
                    std::to_string(m_dx11ShadowMap ? m_dx11ShadowMap->GetInfo().shadowPassCount : 0) +
                    ", lighting passes=" +
                    std::to_string(m_dx11LightingPass ? m_dx11LightingPass->GetInfo().passCount : 0) +
                    ", lighting updates=" +
                    std::to_string(m_dx11LightingPass ? m_dx11LightingPass->GetInfo().parameterUpdateCount : 0) +
                    ", readback frame=" +
                    std::to_string(m_lastGpuFrameBlock.frameIndex) +
                    ", checksumValid=" +
                    std::string(m_lastGpuFrameBlockValid ? "true" : "false") +
                    ", pipeline binds=" +
                    std::to_string(m_dx11PipelineState ? m_dx11PipelineState->GetInfo().bindCount : 0) +
                    ", shadow pipeline binds=" +
                    std::to_string(m_dx11ShadowPipelineState ? m_dx11ShadowPipelineState->GetInfo().bindCount : 0) +
                    ", ssao pipeline binds=" +
                    std::to_string(m_dx11SsaoPipelineState ? m_dx11SsaoPipelineState->GetInfo().bindCount : 0) +
                    ", lighting pipeline binds=" +
                    std::to_string(m_dx11LightingPipelineState ? m_dx11LightingPipelineState->GetInfo().bindCount : 0) +
                    ", transparent passes=" +
                    std::to_string(m_dx11TransparentPass ? m_dx11TransparentPass->GetInfo().passCount : 0) +
                    ", transparent pipeline binds=" +
                    std::to_string(m_dx11TransparentPipelineState ? m_dx11TransparentPipelineState->GetInfo().bindCount : 0) +
                    ", atlas binds=" +
                    std::to_string(m_dx11TextureAtlas ? m_dx11TextureAtlas->GetInfo().bindCount : 0) +
                    ", texture binds=" +
                    std::to_string(m_dx11BootstrapTexture ? m_dx11BootstrapTexture->GetInfo().bindCount : 0) +
                    ", shadow buffer writes=" +
                    std::to_string(m_dx11ShadowPassBuffer ? m_dx11ShadowPassBuffer->GetInfo().writeCount : 0) +
                    ", atlas region=" +
                    std::string(GetActiveDx11AtlasVariant() != nullptr ? GetActiveDx11AtlasVariant()->regionName : "none") +
                    ", geometry binds=" +
                    std::to_string(GetDx11AtlasGeometryBindCount()) +
                    ", geometry draws=" +
                    std::to_string(GetDx11AtlasGeometryDrawCount()) +
                    ", lighting geometry binds=" +
                    std::to_string(m_dx11LightingGeometry ? m_dx11LightingGeometry->GetInfo().bindCount : 0) +
                    ", lighting geometry draws=" +
                    std::to_string(m_dx11LightingGeometry ? m_dx11LightingGeometry->GetInfo().drawCount : 0) +
                    ", transparent geometry binds=" +
                    std::to_string(GetDx11TransparentGeometryBindCount()) +
                    ", transparent geometry draws=" +
                    std::to_string(GetDx11TransparentGeometryDrawCount()) +
                    ", viewport=" +
                    std::to_string(
                        static_cast<std::uint32_t>(m_dx11PipelineState ? m_dx11PipelineState->GetInfo().viewportWidth : 0.0f)) +
                    "x" +
                    std::to_string(
                        static_cast<std::uint32_t>(m_dx11PipelineState ? m_dx11PipelineState->GetInfo().viewportHeight : 0.0f)));
            }
        }
    }

    void OnFixedUpdate(vc::Timestep fixedTimeStep) override
    {
        const auto nextTick = GetFixedTickCount();
        m_systemScheduler.RunStage(vc::SystemStage::FixedUpdate, MakeSystemContext(fixedTimeStep, GetFrameCount(), nextTick));
        if (nextTick > 0 && nextTick % 60 == 0)
        {
            VC_LOG_TRACE("Fixed tick " + std::to_string(nextTick) + " dt=" + std::to_string(fixedTimeStep.GetMilliseconds()) + "ms");
        }
    }

    void OnWindowResize(std::uint32_t width, std::uint32_t height) override
    {
        if (m_dx11RenderTargets)
        {
            m_dx11RenderTargets->Resize(width, height);
        }
        else if (m_dx11SwapChain)
        {
            m_dx11SwapChain->Resize(width, height);
        }

        if (m_dx11GBuffer)
        {
            m_dx11GBuffer->Resize(width, height);
        }

        if (m_dx11PipelineState)
        {
            m_dx11PipelineState->SetViewport(vc::DX11Viewport {
                .x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(width),
                .height = static_cast<float>(height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f
            });
        }

        if (m_dx11LightingPipelineState)
        {
            m_dx11LightingPipelineState->SetViewport(vc::DX11Viewport {
                .x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(width),
                .height = static_cast<float>(height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f
            });
        }

        if (m_dx11TransparentPipelineState)
        {
            m_dx11TransparentPipelineState->SetViewport(vc::DX11Viewport {
                .x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(width),
                .height = static_cast<float>(height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f
            });
        }

        SyncPrimaryCameraAspectRatio(width, height, true);
    }

    void OnShutdown() override
    {
        if (m_primeJob.IsValid() && !m_primeJobReported)
        {
            m_primeJob.Wait();
            m_primeJobReported = true;
            VC_LOG_INFO("Background job joined during shutdown.");
        }

        if (m_particlePool && m_retainedParticle != nullptr)
        {
            m_particlePool->Destroy(m_retainedParticle);
            m_retainedParticle = nullptr;
        }

        m_particlePool.reset();
        m_bootstrapArena.reset();
        if (m_dx11RenderTargets)
        {
            VC_LOG_INFO("DX11 render-target clear count before shutdown: " + std::to_string(m_dx11RenderTargets->GetInfo().clearCount));
        }
        if (m_dx11SwapChain)
        {
            VC_LOG_INFO("DXGI swap chain present count before shutdown: " + std::to_string(m_dx11SwapChain->GetInfo().presentCount));
        }
        if (m_dx11PipelineState)
        {
            const vc::DX11PipelineStateInfo& pipelineInfo = m_dx11PipelineState->GetInfo();
            VC_LOG_INFO(
                "DX11 G-buffer pipeline stats before shutdown: binds=" +
                std::to_string(pipelineInfo.bindCount) +
                ", inputElements=" +
                std::to_string(pipelineInfo.inputElementCount) +
                ", topology=" +
                pipelineInfo.primitiveTopology +
                ", rasterizer=" +
                pipelineInfo.fillMode +
                "/" +
                pipelineInfo.cullMode +
                ", blending=" +
                std::string(pipelineInfo.alphaBlendingEnabled ? "true" : "false") +
                ", viewport=" +
                std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportWidth)) +
                "x" +
                std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportHeight)));
        }
        if (m_dx11LightingPipelineState)
        {
            const vc::DX11PipelineStateInfo& pipelineInfo = m_dx11LightingPipelineState->GetInfo();
            VC_LOG_INFO(
                "DX11 lighting pipeline stats before shutdown: binds=" +
                std::to_string(pipelineInfo.bindCount) +
                ", inputElements=" +
                std::to_string(pipelineInfo.inputElementCount) +
                ", topology=" +
                pipelineInfo.primitiveTopology +
                ", rasterizer=" +
                pipelineInfo.fillMode +
                "/" +
                pipelineInfo.cullMode +
                ", blending=" +
                std::string(pipelineInfo.alphaBlendingEnabled ? "true" : "false") +
                ", viewport=" +
                std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportWidth)) +
                "x" +
                std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportHeight)));
        }
        if (m_dx11ShadowPipelineState)
        {
            const vc::DX11PipelineStateInfo& pipelineInfo = m_dx11ShadowPipelineState->GetInfo();
            VC_LOG_INFO(
                "DX11 shadow pipeline stats before shutdown: binds=" +
                std::to_string(pipelineInfo.bindCount) +
                ", inputElements=" +
                std::to_string(pipelineInfo.inputElementCount) +
                ", topology=" +
                pipelineInfo.primitiveTopology +
                ", rasterizer=" +
                pipelineInfo.fillMode +
                "/" +
                pipelineInfo.cullMode +
                ", blending=" +
                std::string(pipelineInfo.alphaBlendingEnabled ? "true" : "false") +
                ", viewport=" +
                std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportWidth)) +
                "x" +
                std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportHeight)));
        }
        if (m_dx11TransparentPipelineState)
        {
            const vc::DX11PipelineStateInfo& pipelineInfo = m_dx11TransparentPipelineState->GetInfo();
            VC_LOG_INFO(
                "DX11 transparent pipeline stats before shutdown: binds=" +
                std::to_string(pipelineInfo.bindCount) +
                ", inputElements=" +
                std::to_string(pipelineInfo.inputElementCount) +
                ", topology=" +
                pipelineInfo.primitiveTopology +
                ", rasterizer=" +
                pipelineInfo.fillMode +
                "/" +
                pipelineInfo.cullMode +
                ", blending=" +
                std::string(pipelineInfo.alphaBlendingEnabled ? "true" : "false") +
                ", viewport=" +
                std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportWidth)) +
                "x" +
                std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportHeight)));
        }
        if (m_dx11DepthPrePass)
        {
            const vc::DX11DepthPrePassInfo& depthPrePassInfo = m_dx11DepthPrePass->GetInfo();
            VC_LOG_INFO(
                "DX11 depth pre-pass stats before shutdown: depthPasses=" +
                std::to_string(depthPrePassInfo.depthPassCount) +
                ", colorPasses=" +
                std::to_string(depthPrePassInfo.colorPassCount) +
                ", prePassDepth=" +
                depthPrePassInfo.prePassDepthFunction +
                ", colorPassDepth=" +
                depthPrePassInfo.colorPassDepthFunction);
        }
        if (m_dx11GBuffer)
        {
            const vc::DX11GBufferInfo& gBufferInfo = m_dx11GBuffer->GetInfo();
            VC_LOG_INFO(
                "DX11 G-buffer stats before shutdown: binds=" +
                std::to_string(gBufferInfo.bindCount) +
                ", clears=" +
                std::to_string(gBufferInfo.clearCount) +
                ", previewCopies=" +
                std::to_string(gBufferInfo.previewCopyCount) +
                ", targets=" +
                std::to_string(gBufferInfo.renderTargetCount) +
                ", albedo=" +
                gBufferInfo.albedoFormat +
                ", normal=" +
                gBufferInfo.normalFormat +
                ", material=" +
                gBufferInfo.materialFormat +
                ", size=" +
                std::to_string(gBufferInfo.width) +
                "x" +
                std::to_string(gBufferInfo.height));
        }
        if (m_dx11LightingPass)
        {
            const vc::DX11LightingPassInfo& lightingPassInfo = m_dx11LightingPass->GetInfo();
            VC_LOG_INFO(
                "DX11 lighting pass stats before shutdown: passes=" +
                std::to_string(lightingPassInfo.passCount) +
                ", inputBinds=" +
                std::to_string(lightingPassInfo.inputBindCount) +
                ", parameterUpdates=" +
                std::to_string(lightingPassInfo.parameterUpdateCount) +
                ", inputs=" +
                std::to_string(lightingPassInfo.gBufferInputCount) +
                ", albedo=" +
                lightingPassInfo.albedoFormat +
                ", normal=" +
                lightingPassInfo.normalFormat +
                ", material=" +
                lightingPassInfo.materialFormat +
                ", depth=" +
                lightingPassInfo.depthFormat +
                ", sampler=" +
                lightingPassInfo.samplerFilter);
        }
        if (m_dx11TransparentPass)
        {
            const vc::DX11TransparentPassInfo& transparentPassInfo = m_dx11TransparentPass->GetInfo();
            VC_LOG_INFO(
                "DX11 transparent pass stats before shutdown: passes=" +
                std::to_string(transparentPassInfo.passCount) +
                ", depth=" +
                transparentPassInfo.depthFunction +
                ", depthWrites=" +
                std::string(transparentPassInfo.depthWritesEnabled ? "true" : "false") +
                ", alphaBlendRequired=" +
                std::string(transparentPassInfo.alphaBlendingRequired ? "true" : "false"));
        }
        if (m_dx11ShadowMap)
        {
            const vc::DX11ShadowMapInfo& shadowMapInfo = m_dx11ShadowMap->GetInfo();
            VC_LOG_INFO(
                "DX11 shadow map stats before shutdown: binds=" +
                std::to_string(shadowMapInfo.bindCount) +
                ", clears=" +
                std::to_string(shadowMapInfo.clearCount) +
                ", shadowPasses=" +
                std::to_string(shadowMapInfo.shadowPassCount) +
                ", cascades=" +
                std::to_string(shadowMapInfo.cascadeCount) +
                ", size=" +
                std::to_string(shadowMapInfo.width) +
                "x" +
                std::to_string(shadowMapInfo.height) +
                ", dsv=" +
                shadowMapInfo.depthStencilFormat +
                ", srv=" +
                shadowMapInfo.shaderResourceFormat);
        }
        if (!m_dx11AtlasTriangleVariants.empty())
        {
            const SandboxAtlasTriangleVariant* activeVariant = GetActiveDx11AtlasVariant();
            const vc::DX11GeometryBufferInfo& geometryInfo = m_dx11AtlasTriangleVariants.front().geometry->GetInfo();
            VC_LOG_INFO(
                "DX11 geometry stats before shutdown: binds=" +
                std::to_string(GetDx11AtlasGeometryBindCount()) +
                ", draws=" +
                std::to_string(GetDx11AtlasGeometryDrawCount()) +
                ", variants=" +
                std::to_string(m_dx11AtlasTriangleVariants.size()) +
                ", vertices=" +
                std::to_string(geometryInfo.vertexCount) +
                ", indices=" +
                std::to_string(geometryInfo.indexCount) +
                ", vertexStride=" +
                std::to_string(geometryInfo.vertexStride) +
                ", indexFormat=" +
                geometryInfo.indexFormat +
                ", activeRegion=" +
                std::string(activeVariant != nullptr ? activeVariant->regionName : "none"));
        }
        if (!m_dx11AtlasTriangleVariants.empty() && m_dx11AtlasTriangleVariants.front().transparentGeometry)
        {
            const vc::DX11GeometryBufferInfo& geometryInfo = m_dx11AtlasTriangleVariants.front().transparentGeometry->GetInfo();
            VC_LOG_INFO(
                "DX11 transparent geometry stats before shutdown: binds=" +
                std::to_string(GetDx11TransparentGeometryBindCount()) +
                ", draws=" +
                std::to_string(GetDx11TransparentGeometryDrawCount()) +
                ", variants=" +
                std::to_string(m_dx11AtlasTriangleVariants.size()) +
                ", vertices=" +
                std::to_string(geometryInfo.vertexCount) +
                ", indices=" +
                std::to_string(geometryInfo.indexCount) +
                ", vertexStride=" +
                std::to_string(geometryInfo.vertexStride) +
                ", indexFormat=" +
                geometryInfo.indexFormat);
        }
        if (m_dx11LightingGeometry)
        {
            const vc::DX11GeometryBufferInfo& geometryInfo = m_dx11LightingGeometry->GetInfo();
            VC_LOG_INFO(
                "DX11 lighting geometry stats before shutdown: binds=" +
                std::to_string(geometryInfo.bindCount) +
                ", draws=" +
                std::to_string(geometryInfo.drawCount) +
                ", vertices=" +
                std::to_string(geometryInfo.vertexCount) +
                ", indices=" +
                std::to_string(geometryInfo.indexCount) +
                ", vertexStride=" +
                std::to_string(geometryInfo.vertexStride) +
                ", indexFormat=" +
                geometryInfo.indexFormat);
        }
        if (m_dx11TextureAtlas)
        {
            const vc::DX11TextureAtlasInfo& atlasInfo = m_dx11TextureAtlas->GetInfo();
            VC_LOG_INFO(
                "DX11 texture atlas stats before shutdown: binds=" +
                std::to_string(atlasInfo.bindCount) +
                ", regions=" +
                std::to_string(atlasInfo.regionCount) +
                ", size=" +
                std::to_string(atlasInfo.textureWidth) +
                "x" +
                std::to_string(atlasInfo.textureHeight));
        }
        if (m_dx11BootstrapTexture)
        {
            const vc::DX11Texture2DInfo& textureInfo = m_dx11BootstrapTexture->GetInfo();
            VC_LOG_INFO(
                "DX11 texture stats before shutdown: binds=" +
                std::to_string(textureInfo.bindCount) +
                ", size=" +
                std::to_string(textureInfo.width) +
                "x" +
                std::to_string(textureInfo.height) +
                ", mips=" +
                std::to_string(textureInfo.mipLevels) +
                ", format=" +
                textureInfo.format +
                ", path=" +
                textureInfo.sourcePath.string());
        }
        VC_LOG_INFO(
            "DX11 shader compile stats before shutdown: shaders=" +
            std::to_string(m_shaderCompileCount) +
            ", bytes=" +
            std::to_string(m_shaderCompileByteSize) +
            ", smokeTestPassed=" +
            std::string(m_shaderCompilerSmokeTestPassed ? "true" : "false"));
        if (m_dx11DynamicBuffer)
        {
            const vc::DX11BufferInfo& bufferInfo = m_dx11DynamicBuffer->GetInfo();
            VC_LOG_INFO(
                "DX11 upload buffer stats before shutdown: writes=" +
                std::to_string(bufferInfo.writeCount) +
                ", maps=" +
                std::to_string(bufferInfo.mapCount) +
                ", lastMapMode=" +
                bufferInfo.lastMapMode);
        }
        if (m_dx11ReadbackBuffer)
        {
            const vc::DX11BufferInfo& bufferInfo = m_dx11ReadbackBuffer->GetInfo();
            VC_LOG_INFO(
                "DX11 readback buffer stats before shutdown: copies=" +
                std::to_string(bufferInfo.copyCount) +
                ", maps=" +
                std::to_string(bufferInfo.mapCount) +
                ", lastMapMode=" +
                bufferInfo.lastMapMode +
                ", lastFrame=" +
                std::to_string(m_lastGpuFrameBlock.frameIndex) +
                ", checksumValid=" +
                std::string(m_lastGpuFrameBlockValid ? "true" : "false"));
        }
        if (m_dx11ShadowPassBuffer)
        {
            const vc::DX11BufferInfo& bufferInfo = m_dx11ShadowPassBuffer->GetInfo();
            VC_LOG_INFO(
                "DX11 shadow buffer stats before shutdown: writes=" +
                std::to_string(bufferInfo.writeCount) +
                ", maps=" +
                std::to_string(bufferInfo.mapCount) +
                ", lastMapMode=" +
                bufferInfo.lastMapMode);
        }
        if (m_dx11ContextSync)
        {
            const bool gpuIdle = m_dx11ContextSync->WaitForGpuIdle(2000);
            const vc::DX11ContextSyncInfo& contextSyncInfo = m_dx11ContextSync->GetInfo();
            VC_LOG_INFO(
                "DX11 context sync before shutdown: gpuIdle=" +
                std::string(gpuIdle ? "true" : "false") +
                ", queryReady=" +
                std::string(contextSyncInfo.queryReady ? "true" : "false") +
                ", flushCount=" +
                std::to_string(contextSyncInfo.flushCount) +
                ", waitCount=" +
                std::to_string(contextSyncInfo.waitCount) +
                ", waitTimeouts=" +
                std::to_string(contextSyncInfo.waitTimeoutCount) +
                ", lastWaitMs=" +
                std::to_string(contextSyncInfo.lastWaitDurationMilliseconds) +
                ", maxWaitMs=" +
                std::to_string(contextSyncInfo.maxWaitDurationMilliseconds));
        }
        m_dx11AtlasTriangleVariants.clear();
        m_dx11LightingGeometry.reset();
        m_dx11TextureAtlas.reset();
        m_dx11BootstrapTexture.reset();
        m_dx11ShadowPassBuffer.reset();
        m_dx11ShadowPipelineState.reset();
        m_dx11ShadowMap.reset();
        m_dx11TransparentPass.reset();
        m_dx11TransparentPipelineState.reset();
        m_dx11LightingPipelineState.reset();
        m_dx11PipelineState.reset();
        m_dx11LightingPass.reset();
        m_dx11DepthPrePass.reset();
        m_dx11GBuffer.reset();
        m_dx11ReadbackBuffer.reset();
        m_dx11DynamicBuffer.reset();
        m_dx11ContextSync.reset();
        m_dx11RenderTargets.reset();
        m_dx11SwapChain.reset();
        m_dx11Device.reset();
        VC_LOG_INFO("DX11 bootstrap shutdown complete.");
        const vc::MemoryStatistics finalMemoryStats = vc::Memory::GetStatistics();
        VC_LOG_INFO(
            "Memory stats after allocator teardown: active=" +
            std::to_string(finalMemoryStats.activeBytes) +
            " bytes, peak=" +
            std::to_string(finalMemoryStats.peakActiveBytes) +
            " bytes");

        m_systemScheduler.RunStage(vc::SystemStage::Shutdown, MakeSystemContext());
        m_systemScheduler.Clear();
        m_registry.Clear();
        VC_LOG_INFO("Registry cleared during shutdown, alive=" + std::to_string(m_registry.GetAliveCount()));

        m_configWatcher.reset();
        VC_LOG_INFO("Sandbox shutdown hook executed.");
    }

private:
    void RegisterSandboxSystems()
    {
        m_systemScheduler.Clear();

        (void)m_systemScheduler.RegisterSystem(
            "StartupPreparePlayerMotion",
            vc::SystemStage::Startup,
            [this](vc::SystemContext& context)
            {
                vc::Entity player = context.registry.Wrap(m_playerEntityId);
                if (!player)
                {
                    return;
                }

                if (!player.HasComponent<SandboxVelocityComponent>())
                {
                    const vc::TransformComponent& transform = player.GetComponent<vc::TransformComponent>();
                    const vc::Vec3 forward = transform.GetForwardDirection();
                    player.AddComponent<SandboxVelocityComponent>(SandboxVelocityComponent {
                        .linear = forward * 3.0f + vc::Vec3(0.0f, 0.5f, 0.0f)
                    });
                }

                const SandboxVelocityComponent& velocity = player.GetComponent<SandboxVelocityComponent>();
                VC_LOG_INFO("Startup system attached player velocity component: " + FormatVec3(velocity.linear));
            },
            5);

        (void)m_systemScheduler.RegisterSystem(
            "StartupAudit",
            vc::SystemStage::Startup,
            [this](vc::SystemContext& context)
            {
                const vc::Entity player = context.registry.Wrap(m_playerEntityId);
                const bool hasVelocity = static_cast<bool>(player) && player.HasComponent<SandboxVelocityComponent>();
                const bool hasTransform = static_cast<bool>(player) && player.HasComponent<vc::TransformComponent>();
                const vc::Entity cameraEntity = context.registry.Wrap(m_cameraEntityId);
                const bool hasCamera = static_cast<bool>(cameraEntity) && cameraEntity.HasComponent<vc::CameraComponent>();
                bool transformIsIdentity = false;
                bool scaleIsValid = false;
                bool projectionIsValid = false;
                if (hasTransform)
                {
                    const vc::TransformComponent& transform = player.GetComponent<vc::TransformComponent>();
                    transformIsIdentity = transform.IsIdentity();
                    scaleIsValid = transform.HasValidScale();
                }
                if (hasCamera)
                {
                    const vc::CameraComponent& camera = cameraEntity.GetComponent<vc::CameraComponent>();
                    projectionIsValid = camera.HasValidProjection();
                }

                VC_LOG_INFO(
                    "Startup audit: alive=" +
                    std::to_string(context.registry.GetAliveCount()) +
                    ", player has velocity=" +
                    std::string(hasVelocity ? "true" : "false") +
                    ", transform identity=" +
                    std::string(transformIsIdentity ? "true" : "false") +
                    ", scale valid=" +
                    std::string(scaleIsValid ? "true" : "false") +
                    ", camera ready=" +
                    std::string(hasCamera ? "true" : "false") +
                    ", projection valid=" +
                    std::string(projectionIsValid ? "true" : "false"));
            },
            20);

        (void)m_systemScheduler.RegisterSystem(
            "MovementSystem",
            vc::SystemStage::Update,
            [](vc::SystemContext& context)
            {
                const float deltaSeconds = static_cast<float>(context.deltaTime.GetSeconds());
                context.registry.View<vc::TransformComponent, SandboxVelocityComponent>(
                    [deltaSeconds](vc::Entity, vc::TransformComponent& transform, SandboxVelocityComponent& velocity)
                    {
                        transform.Translate(velocity.linear * deltaSeconds);
                    });
            },
            10);

        (void)m_systemScheduler.RegisterSystem(
            "CameraFollowPlayer",
            vc::SystemStage::Update,
            [this](vc::SystemContext& context)
            {
                const vc::Entity player = context.registry.Wrap(m_playerEntityId);
                vc::Entity cameraEntity = context.registry.Wrap(m_cameraEntityId);
                if (!player
                    || !cameraEntity
                    || !player.HasComponent<vc::TransformComponent>()
                    || !cameraEntity.HasComponent<vc::TransformComponent>()
                    || !cameraEntity.HasComponent<vc::CameraComponent>())
                {
                    return;
                }

                const vc::TransformComponent& playerTransform = player.GetComponent<vc::TransformComponent>();
                vc::TransformComponent& cameraTransform = cameraEntity.GetComponent<vc::TransformComponent>();
                vc::CameraComponent& camera = cameraEntity.GetComponent<vc::CameraComponent>();

                const vc::Vec3 followOffset = -playerTransform.GetForwardDirection() * 9.0f + vc::Vec3(0.0f, 3.25f, 0.0f);
                cameraTransform.translation = playerTransform.translation + followOffset;
                cameraTransform.rotationEulerDegrees = vc::Vec3(18.0f, playerTransform.rotationEulerDegrees.y, 0.0f);
                camera.SetAspectRatio(GetWindowAspectRatio());
            },
            20);

        (void)m_systemScheduler.RegisterSystem(
            "PlayerTelemetry",
            vc::SystemStage::Update,
            [this](vc::SystemContext& context)
            {
                if (context.frameIndex != 1 && context.frameIndex % 120 != 0)
                {
                    return;
                }

                const vc::Entity player = context.registry.Wrap(m_playerEntityId);
                if (!player || !player.HasComponent<vc::TransformComponent>() || !player.HasComponent<SandboxHealthComponent>())
                {
                    return;
                }

                const vc::TransformComponent& transform = player.GetComponent<vc::TransformComponent>();
                const SandboxHealthComponent& health = player.GetComponent<SandboxHealthComponent>();
                VC_LOG_INFO(
                    "Update systems frame " +
                    std::to_string(context.frameIndex) +
                    ": player pos=" +
                    FormatVec3(transform.translation) +
                    ", forward=" +
                    FormatVec3(transform.GetForwardDirection()) +
                    ", scale=" +
                    FormatVec3(transform.scale) +
                    ", health=" +
                    std::to_string(health.value));
            },
            30);

        (void)m_systemScheduler.RegisterSystem(
            "CameraTelemetry",
            vc::SystemStage::Update,
            [this](vc::SystemContext& context)
            {
                if (context.frameIndex != 1 && context.frameIndex % 120 != 0)
                {
                    return;
                }

                const vc::Entity cameraEntity = context.registry.Wrap(m_cameraEntityId);
                if (!cameraEntity
                    || !cameraEntity.HasComponent<vc::TransformComponent>()
                    || !cameraEntity.HasComponent<vc::CameraComponent>())
                {
                    return;
                }

                const vc::TransformComponent& cameraTransform = cameraEntity.GetComponent<vc::TransformComponent>();
                const vc::CameraComponent& camera = cameraEntity.GetComponent<vc::CameraComponent>();
                VC_LOG_INFO(
                    "Camera systems frame " +
                    std::to_string(context.frameIndex) +
                    ": type=" +
                    std::string(vc::ToString(camera.projectionType)) +
                    ", aspect=" +
                    std::to_string(camera.aspectRatio) +
                    ", nearPlane=" +
                    std::to_string(camera.GetNearPlaneWidth()) +
                    "x" +
                    std::to_string(camera.GetNearPlaneHeight()) +
                    ", pos=" +
                    FormatVec3(cameraTransform.translation) +
                    ", forward=" +
                    FormatVec3(cameraTransform.GetForwardDirection()));
            },
            40);

        (void)m_systemScheduler.RegisterSystem(
            "FixedHealthDrain",
            vc::SystemStage::FixedUpdate,
            [this](vc::SystemContext& context)
            {
                if (context.fixedTickIndex == 0 || context.fixedTickIndex % 60 != 0)
                {
                    return;
                }

                vc::Entity player = context.registry.Wrap(m_playerEntityId);
                if (!player || !player.HasComponent<SandboxHealthComponent>())
                {
                    return;
                }

                SandboxHealthComponent& health = player.GetComponent<SandboxHealthComponent>();
                if (health.value > 0)
                {
                    --health.value;
                }
            },
            10);

        (void)m_systemScheduler.RegisterSystem(
            "FixedTelemetry",
            vc::SystemStage::FixedUpdate,
            [this](vc::SystemContext& context)
            {
                if (context.fixedTickIndex == 0 || context.fixedTickIndex % 120 != 0)
                {
                    return;
                }

                const vc::Entity player = context.registry.Wrap(m_playerEntityId);
                if (!player || !player.HasComponent<SandboxHealthComponent>())
                {
                    return;
                }

                const SandboxHealthComponent& health = player.GetComponent<SandboxHealthComponent>();
                VC_LOG_INFO(
                    "Fixed systems tick " +
                    std::to_string(context.fixedTickIndex) +
                    ": player health=" +
                    std::to_string(health.value));
            },
            20);

        (void)m_systemScheduler.RegisterSystem(
            "ShutdownAudit",
            vc::SystemStage::Shutdown,
            [](vc::SystemContext& context)
            {
                VC_LOG_INFO("Shutdown systems see alive entities=" + std::to_string(context.registry.GetAliveCount()));
            },
            10);
    }

    void LogSystemOrder(vc::SystemStage stage) const
    {
        VC_LOG_INFO(
            "System stage " +
            std::string(vc::ToString(stage)) +
            " order: " +
            JoinLabels(m_systemScheduler.GetExecutionOrder(stage)));
    }

    [[nodiscard]] vc::SystemContext MakeSystemContext(
        vc::Timestep deltaTime = vc::Timestep(),
        std::uint64_t frameIndex = 0,
        std::uint64_t fixedTickIndex = 0)
    {
        vc::SystemContext context { m_registry };
        context.deltaTime = deltaTime;
        context.frameIndex = frameIndex;
        context.fixedTickIndex = fixedTickIndex;
        return context;
    }

    [[nodiscard]] float GetWindowAspectRatio() const
    {
        const std::uint32_t width = GetWindow().GetWidth();
        const std::uint32_t height = GetWindow().GetHeight();
        return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 16.0f / 9.0f;
    }

    void RunDx11ShaderCompilerSmokeTest()
    {
        static constexpr std::string_view kInlinePixelShader = R"(
float4 SandboxInlinePixelMain() : SV_TARGET
{
#if VC_SANDBOX_TINT
    return float4(0.20f, 0.45f, 0.80f, 1.0f);
#else
    return float4(0.95f, 0.25f, 0.20f, 1.0f);
#endif
}
)";

        const vc::Path shaderPath = ResolveSandboxShaderPath();

        vc::DX11ShaderCompileSpecification vertexSpecification {};
        vertexSpecification.entryPoint = "SandboxVertexMain";
        vertexSpecification.stage = vc::DX11ShaderStage::Vertex;
        m_vertexShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(shaderPath, std::move(vertexSpecification));

        vc::DX11ShaderCompileSpecification gBufferPixelSpecification {};
        gBufferPixelSpecification.entryPoint = "SandboxGBufferPixelMain";
        gBufferPixelSpecification.stage = vc::DX11ShaderStage::Pixel;
        m_pixelShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(shaderPath, std::move(gBufferPixelSpecification));

        vc::DX11ShaderCompileSpecification lightingVertexSpecification {};
        lightingVertexSpecification.entryPoint = "SandboxLightingVertexMain";
        lightingVertexSpecification.stage = vc::DX11ShaderStage::Vertex;
        m_lightingVertexShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(
            shaderPath,
            std::move(lightingVertexSpecification));

        vc::DX11ShaderCompileSpecification lightingPixelSpecification {};
        lightingPixelSpecification.entryPoint = "SandboxLightingPixelMain";
        lightingPixelSpecification.stage = vc::DX11ShaderStage::Pixel;
        m_lightingPixelShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(
            shaderPath,
            std::move(lightingPixelSpecification));

        vc::DX11ShaderCompileSpecification ssaoPixelSpecification {};
        ssaoPixelSpecification.entryPoint = "SandboxSsaoPixelMain";
        ssaoPixelSpecification.stage = vc::DX11ShaderStage::Pixel;
        m_ssaoPixelShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(
            shaderPath,
            std::move(ssaoPixelSpecification));

        vc::DX11ShaderCompileSpecification transparentPixelSpecification {};
        transparentPixelSpecification.entryPoint = "SandboxTransparentPixelMain";
        transparentPixelSpecification.stage = vc::DX11ShaderStage::Pixel;
        m_transparentPixelShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(
            shaderPath,
            std::move(transparentPixelSpecification));

        vc::DX11ShaderCompileSpecification shadowVertexSpecification {};
        shadowVertexSpecification.entryPoint = "SandboxShadowVertexMain";
        shadowVertexSpecification.stage = vc::DX11ShaderStage::Vertex;
        m_shadowVertexShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(
            shaderPath,
            std::move(shadowVertexSpecification));

        vc::DX11ShaderCompileSpecification shadowPixelSpecification {};
        shadowPixelSpecification.entryPoint = "SandboxShadowPixelMain";
        shadowPixelSpecification.stage = vc::DX11ShaderStage::Pixel;
        m_shadowPixelShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(
            shaderPath,
            std::move(shadowPixelSpecification));

        vc::DX11ShaderCompileSpecification inlinePixelSpecification {};
        inlinePixelSpecification.sourceName = "SandboxInlinePixel";
        inlinePixelSpecification.entryPoint = "SandboxInlinePixelMain";
        inlinePixelSpecification.stage = vc::DX11ShaderStage::Pixel;
        inlinePixelSpecification.macros.push_back(vc::DX11ShaderMacro { .name = "VC_SANDBOX_TINT", .value = "1" });
        const vc::DX11ShaderBytecode inlinePixelShader = vc::DX11ShaderCompiler::CompileFromSource(
            kInlinePixelShader,
            std::move(inlinePixelSpecification));

        VC_ASSERT(m_vertexShaderBytecode.IsValid(), "Sandbox vertex shader bytecode is invalid.");
        VC_ASSERT(m_pixelShaderBytecode.IsValid(), "Sandbox pixel shader bytecode is invalid.");
        VC_ASSERT(m_lightingVertexShaderBytecode.IsValid(), "Sandbox lighting vertex shader bytecode is invalid.");
        VC_ASSERT(m_lightingPixelShaderBytecode.IsValid(), "Sandbox lighting pixel shader bytecode is invalid.");
        VC_ASSERT(m_ssaoPixelShaderBytecode.IsValid(), "Sandbox SSAO pixel shader bytecode is invalid.");
        VC_ASSERT(m_transparentPixelShaderBytecode.IsValid(), "Sandbox transparent pixel shader bytecode is invalid.");
        VC_ASSERT(m_shadowVertexShaderBytecode.IsValid(), "Sandbox shadow vertex shader bytecode is invalid.");
        VC_ASSERT(m_shadowPixelShaderBytecode.IsValid(), "Sandbox shadow pixel shader bytecode is invalid.");
        VC_ASSERT(inlinePixelShader.IsValid(), "Sandbox inline pixel shader bytecode is invalid.");
        VC_ASSERT(m_vertexShaderBytecode.GetData() != nullptr, "Sandbox vertex shader bytecode pointer is null.");
        VC_ASSERT(m_pixelShaderBytecode.GetData() != nullptr, "Sandbox pixel shader bytecode pointer is null.");
        VC_ASSERT(
            m_lightingVertexShaderBytecode.GetData() != nullptr,
            "Sandbox lighting vertex shader bytecode pointer is null.");
        VC_ASSERT(
            m_lightingPixelShaderBytecode.GetData() != nullptr,
            "Sandbox lighting pixel shader bytecode pointer is null.");
        VC_ASSERT(
            m_ssaoPixelShaderBytecode.GetData() != nullptr,
            "Sandbox SSAO pixel shader bytecode pointer is null.");
        VC_ASSERT(
            m_transparentPixelShaderBytecode.GetData() != nullptr,
            "Sandbox transparent pixel shader bytecode pointer is null.");
        VC_ASSERT(
            m_shadowVertexShaderBytecode.GetData() != nullptr,
            "Sandbox shadow vertex shader bytecode pointer is null.");
        VC_ASSERT(
            m_shadowPixelShaderBytecode.GetData() != nullptr,
            "Sandbox shadow pixel shader bytecode pointer is null.");
        VC_ASSERT(inlinePixelShader.GetData() != nullptr, "Sandbox inline pixel shader bytecode pointer is null.");

        m_shaderCompileCount = 9;
        m_shaderCompileByteSize = m_vertexShaderBytecode.GetInfo().sizeInBytes
            + m_pixelShaderBytecode.GetInfo().sizeInBytes
            + m_lightingVertexShaderBytecode.GetInfo().sizeInBytes
            + m_lightingPixelShaderBytecode.GetInfo().sizeInBytes
            + m_ssaoPixelShaderBytecode.GetInfo().sizeInBytes
            + m_transparentPixelShaderBytecode.GetInfo().sizeInBytes
            + m_shadowVertexShaderBytecode.GetInfo().sizeInBytes
            + m_shadowPixelShaderBytecode.GetInfo().sizeInBytes
            + inlinePixelShader.GetInfo().sizeInBytes;
        m_shaderCompilerSmokeTestPassed = true;

        VC_LOG_INFO(
            "Sandbox HLSL compile: fileVS=" +
            std::to_string(m_vertexShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, fileGBufferPS=" +
            std::to_string(m_pixelShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, fileLightingVS=" +
            std::to_string(m_lightingVertexShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, fileLightingPS=" +
            std::to_string(m_lightingPixelShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, fileSsaoPS=" +
            std::to_string(m_ssaoPixelShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, fileTransparentPS=" +
            std::to_string(m_transparentPixelShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, fileShadowVS=" +
            std::to_string(m_shadowVertexShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, fileShadowPS=" +
            std::to_string(m_shadowPixelShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, inlinePS=" +
            std::to_string(inlinePixelShader.GetInfo().sizeInBytes) +
            " bytes, total=" +
            std::to_string(m_shaderCompileByteSize) +
            ", warnings=" +
            std::string(
                m_vertexShaderBytecode.GetInfo().hasWarnings
                        || m_pixelShaderBytecode.GetInfo().hasWarnings
                        || m_lightingVertexShaderBytecode.GetInfo().hasWarnings
                        || m_lightingPixelShaderBytecode.GetInfo().hasWarnings
                        || m_ssaoPixelShaderBytecode.GetInfo().hasWarnings
                        || m_transparentPixelShaderBytecode.GetInfo().hasWarnings
                        || m_shadowVertexShaderBytecode.GetInfo().hasWarnings
                        || m_shadowPixelShaderBytecode.GetInfo().hasWarnings
                        || inlinePixelShader.GetInfo().hasWarnings
                    ? "true"
                    : "false") +
            ", file=" +
            shaderPath.string());
    }

    void InitializeDx11ShadowMap()
    {
        VC_ASSERT(m_dx11Device != nullptr, "DX11 shadow-map initialization requires a ready device.");

        m_dx11ShadowMap = vc::CreateScope<vc::DX11ShadowMap>(
            *m_dx11Device,
            vc::DX11ShadowMapSpecification {
                .width = 1024,
                .height = 1024,
                .cascadeCount = static_cast<std::uint32_t>(vc::DX11ShadowCascadeCount)
            });

        const vc::DX11ShadowMapInfo& shadowMapInfo = m_dx11ShadowMap->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 shadow map: size=" +
            std::to_string(shadowMapInfo.width) +
            "x" +
            std::to_string(shadowMapInfo.height) +
            ", cascades=" +
            std::to_string(shadowMapInfo.cascadeCount) +
            ", depth=" +
            shadowMapInfo.depthStencilFormat +
            ", srv=" +
            shadowMapInfo.shaderResourceFormat);
    }

    void InitializeDx11ShadowPipelineState()
    {
        VC_ASSERT(m_shadowVertexShaderBytecode.IsValid(), "DX11 shadow pipeline initialization requires a compiled vertex shader.");
        VC_ASSERT(m_shadowPixelShaderBytecode.IsValid(), "DX11 shadow pipeline initialization requires a compiled pixel shader.");
        VC_ASSERT(m_dx11Device != nullptr, "DX11 shadow pipeline initialization requires a ready device.");
        VC_ASSERT(m_dx11ShadowMap != nullptr, "DX11 shadow pipeline initialization requires a shadow map.");

        vc::DX11PipelineStateSpecification specification {};
        specification.vertexShader = &m_shadowVertexShaderBytecode;
        specification.pixelShader = &m_shadowPixelShaderBytecode;
        specification.inputElements = {
            vc::DX11InputElement {
                .semanticName = "POSITION",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float3,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, position)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            },
            vc::DX11InputElement {
                .semanticName = "COLOR",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float4,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, color)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            },
            vc::DX11InputElement {
                .semanticName = "TEXCOORD",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float2,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, uv)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            }
        };
        specification.primitiveTopology = vc::DX11PrimitiveTopology::TriangleList;
        specification.rasterizer.fillMode = vc::DX11FillMode::Solid;
        specification.rasterizer.cullMode = vc::DX11CullMode::None;
        specification.rasterizer.depthClipEnable = true;
        specification.blend.enableBlending = false;
        specification.viewport = vc::DX11Viewport {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(m_dx11ShadowMap->GetInfo().width),
            .height = static_cast<float>(m_dx11ShadowMap->GetInfo().height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };

        m_dx11ShadowPipelineState = vc::CreateScope<vc::DX11PipelineState>(*m_dx11Device, std::move(specification));
        const vc::DX11PipelineStateInfo& pipelineInfo = m_dx11ShadowPipelineState->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 shadow pipeline: topology=" +
            pipelineInfo.primitiveTopology +
            ", inputElements=" +
            std::to_string(pipelineInfo.inputElementCount) +
            ", rasterizer=" +
            pipelineInfo.fillMode +
            "/" +
            pipelineInfo.cullMode +
            ", viewport=" +
            std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportWidth)) +
            "x" +
            std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportHeight)));
    }

    void InitializeDx11ShadowPassBuffer()
    {
        VC_ASSERT(m_dx11Device != nullptr, "DX11 shadow buffer initialization requires a ready device.");

        m_dx11ShadowPassBuffer = vc::CreateScope<vc::DX11Buffer>(
            *m_dx11Device,
            vc::DX11BufferSpecification {
                .sizeInBytes = sizeof(SandboxShadowPassBlock),
                .stride = sizeof(SandboxShadowPassBlock),
                .kind = vc::DX11BufferKind::Constant,
                .usage = vc::DX11BufferUsage::Dynamic,
                .cpuReadable = false,
                .cpuWritable = true
            });

        const vc::DX11BufferInfo& bufferInfo = m_dx11ShadowPassBuffer->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 shadow buffer: kind=" +
            bufferInfo.kind +
            ", usage=" +
            bufferInfo.usage +
            ", bytes=" +
            std::to_string(bufferInfo.sizeInBytes));
    }

    void InitializeDx11PipelineState()
    {
        VC_ASSERT(m_vertexShaderBytecode.IsValid(), "DX11 pipeline initialization requires a compiled vertex shader.");
        VC_ASSERT(m_pixelShaderBytecode.IsValid(), "DX11 pipeline initialization requires a compiled pixel shader.");
        VC_ASSERT(m_dx11Device != nullptr, "DX11 pipeline initialization requires a ready device.");

        vc::DX11PipelineStateSpecification specification {};
        specification.vertexShader = &m_vertexShaderBytecode;
        specification.pixelShader = &m_pixelShaderBytecode;
        specification.inputElements = {
            vc::DX11InputElement {
                .semanticName = "POSITION",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float3,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, position)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            },
            vc::DX11InputElement {
                .semanticName = "COLOR",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float4,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, color)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            },
            vc::DX11InputElement {
                .semanticName = "TEXCOORD",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float2,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, uv)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            }
        };
        specification.primitiveTopology = vc::DX11PrimitiveTopology::TriangleList;
        specification.rasterizer.fillMode = vc::DX11FillMode::Solid;
        specification.rasterizer.cullMode = vc::DX11CullMode::None;
        specification.rasterizer.depthClipEnable = true;
        specification.blend.enableBlending = false;
        specification.viewport = vc::DX11Viewport {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(GetWindow().GetWidth()),
            .height = static_cast<float>(GetWindow().GetHeight()),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };

        m_dx11PipelineState = vc::CreateScope<vc::DX11PipelineState>(*m_dx11Device, std::move(specification));
        const vc::DX11PipelineStateInfo& pipelineInfo = m_dx11PipelineState->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 pipeline: topology=" +
            pipelineInfo.primitiveTopology +
            ", inputElements=" +
            std::to_string(pipelineInfo.inputElementCount) +
            ", rasterizer=" +
            pipelineInfo.fillMode +
            "/" +
            pipelineInfo.cullMode +
            ", blending=" +
            std::string(pipelineInfo.alphaBlendingEnabled ? "true" : "false") +
            ", viewport=" +
            std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportWidth)) +
            "x" +
            std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportHeight)));
    }

    void InitializeDx11Geometry()
    {
        VC_ASSERT(m_dx11Device != nullptr, "DX11 geometry initialization requires a ready device.");
        VC_ASSERT(m_dx11TextureAtlas != nullptr, "DX11 geometry initialization requires a ready texture atlas.");

        static constexpr std::array<std::uint16_t, 12> kSceneIndices { 0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7 };
        static constexpr std::array<std::uint16_t, 6> kTransparentIndices { 0, 1, 2, 0, 2, 3 };
        m_dx11AtlasTriangleVariants.clear();

        for (const vc::DX11TextureAtlasRegion& region : m_dx11TextureAtlas->GetRegions())
        {
            const std::array<SandboxVertex, 8> sceneVertices {
                SandboxVertex {
                    .position = { -0.88f, 0.12f, 0.18f },
                    .color = { 0.95f, 0.95f, 0.95f, 1.0f },
                    .uv = { region.uMin, region.vMin }
                },
                SandboxVertex {
                    .position = { 0.72f, 0.12f, 0.18f },
                    .color = { 0.95f, 0.95f, 0.95f, 1.0f },
                    .uv = { region.uMax, region.vMin }
                },
                SandboxVertex {
                    .position = { 0.72f, -0.88f, 0.18f },
                    .color = { 0.95f, 0.95f, 0.95f, 1.0f },
                    .uv = { region.uMax, region.vMax }
                },
                SandboxVertex {
                    .position = { -0.88f, -0.88f, 0.18f },
                    .color = { 0.95f, 0.95f, 0.95f, 1.0f },
                    .uv = { region.uMin, region.vMax }
                },
                SandboxVertex {
                    .position = { -0.30f, 0.52f, 0.62f },
                    .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                    .uv = { region.uMin, region.vMin }
                },
                SandboxVertex {
                    .position = { 0.08f, 0.52f, 0.62f },
                    .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                    .uv = { region.uMax, region.vMin }
                },
                SandboxVertex {
                    .position = { 0.18f, -0.08f, 0.62f },
                    .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                    .uv = { region.uMax, region.vMax }
                },
                SandboxVertex {
                    .position = { -0.20f, -0.08f, 0.62f },
                    .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                    .uv = { region.uMin, region.vMax }
                }
            };
            const std::array<SandboxVertex, 4> transparentVertices {
                SandboxVertex {
                    .position = { -0.56f, 0.64f, 0.42f },
                    .color = { 0.72f, 0.95f, 1.00f, 0.52f },
                    .uv = { region.uMin, region.vMin }
                },
                SandboxVertex {
                    .position = { 0.46f, 0.58f, 0.42f },
                    .color = { 0.72f, 0.95f, 1.00f, 0.52f },
                    .uv = { region.uMax, region.vMin }
                },
                SandboxVertex {
                    .position = { 0.56f, -0.52f, 0.42f },
                    .color = { 0.72f, 0.95f, 1.00f, 0.52f },
                    .uv = { region.uMax, region.vMax }
                },
                SandboxVertex {
                    .position = { -0.48f, -0.42f, 0.42f },
                    .color = { 0.72f, 0.95f, 1.00f, 0.52f },
                    .uv = { region.uMin, region.vMax }
                }
            };

            SandboxAtlasTriangleVariant variant {};
            variant.regionName = region.name;
            variant.uvSummary = FormatUvRect(region.uMin, region.vMin, region.uMax, region.vMax);
            variant.geometry = vc::CreateScope<vc::DX11GeometryBuffer>(
                *m_dx11Device,
                vc::DX11GeometryBufferSpecification {
                    .vertexData = sceneVertices.data(),
                    .vertexCount = static_cast<std::uint32_t>(sceneVertices.size()),
                    .vertexStride = sizeof(SandboxVertex),
                    .indexData = kSceneIndices.data(),
                    .indexCount = static_cast<std::uint32_t>(kSceneIndices.size()),
                    .indexFormat = vc::DX11IndexFormat::UInt16,
                    .usage = vc::DX11BufferUsage::Immutable
                });
            variant.transparentGeometry = vc::CreateScope<vc::DX11GeometryBuffer>(
                *m_dx11Device,
                vc::DX11GeometryBufferSpecification {
                    .vertexData = transparentVertices.data(),
                    .vertexCount = static_cast<std::uint32_t>(transparentVertices.size()),
                    .vertexStride = sizeof(SandboxVertex),
                    .indexData = kTransparentIndices.data(),
                    .indexCount = static_cast<std::uint32_t>(kTransparentIndices.size()),
                    .indexFormat = vc::DX11IndexFormat::UInt16,
                    .usage = vc::DX11BufferUsage::Immutable
                });
            m_dx11AtlasTriangleVariants.push_back(std::move(variant));
        }

        VC_ASSERT(!m_dx11AtlasTriangleVariants.empty(), "DX11 atlas geometry initialization produced no variants.");
        m_activeDx11AtlasVariantIndex = 0;

        const vc::DX11GeometryBufferInfo& geometryInfo = m_dx11AtlasTriangleVariants.front().geometry->GetInfo();
        const SandboxAtlasTriangleVariant& initialVariant = m_dx11AtlasTriangleVariants.front();
        VC_LOG_INFO(
            "Sandbox DX11 atlas geometry: variants=" +
            std::to_string(m_dx11AtlasTriangleVariants.size()) +
            ", initialRegion=" +
            initialVariant.regionName +
            ", uv=" +
            initialVariant.uvSummary +
            ", opaqueIndices=" +
            std::to_string(geometryInfo.indexCount) +
            ", transparentIndices=" +
            std::to_string(initialVariant.transparentGeometry->GetInfo().indexCount) +
            ", vertexStride=" +
            std::to_string(geometryInfo.vertexStride) +
            ", transparentAlphaGeometry=true");
    }

    void InitializeDx11LightingPipelineState()
    {
        VC_ASSERT(
            m_lightingVertexShaderBytecode.IsValid(),
            "DX11 lighting pipeline initialization requires a compiled fullscreen vertex shader.");
        VC_ASSERT(
            m_lightingPixelShaderBytecode.IsValid(),
            "DX11 lighting pipeline initialization requires a compiled lighting pixel shader.");
        VC_ASSERT(m_dx11Device != nullptr, "DX11 lighting pipeline initialization requires a ready device.");

        vc::DX11PipelineStateSpecification specification {};
        specification.vertexShader = &m_lightingVertexShaderBytecode;
        specification.pixelShader = &m_lightingPixelShaderBytecode;
        specification.inputElements = {
            vc::DX11InputElement {
                .semanticName = "POSITION",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float2,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxFullscreenVertex, position)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            },
            vc::DX11InputElement {
                .semanticName = "TEXCOORD",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float2,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxFullscreenVertex, uv)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            }
        };
        specification.primitiveTopology = vc::DX11PrimitiveTopology::TriangleList;
        specification.rasterizer.fillMode = vc::DX11FillMode::Solid;
        specification.rasterizer.cullMode = vc::DX11CullMode::None;
        specification.rasterizer.depthClipEnable = true;
        specification.blend.enableBlending = false;
        specification.viewport = vc::DX11Viewport {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(GetWindow().GetWidth()),
            .height = static_cast<float>(GetWindow().GetHeight()),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };

        m_dx11LightingPipelineState = vc::CreateScope<vc::DX11PipelineState>(*m_dx11Device, std::move(specification));
        const vc::DX11PipelineStateInfo& pipelineInfo = m_dx11LightingPipelineState->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 lighting pipeline: topology=" +
            pipelineInfo.primitiveTopology +
            ", inputElements=" +
            std::to_string(pipelineInfo.inputElementCount) +
            ", rasterizer=" +
            pipelineInfo.fillMode +
            "/" +
            pipelineInfo.cullMode +
            ", blending=" +
            std::string(pipelineInfo.alphaBlendingEnabled ? "true" : "false") +
            ", viewport=" +
            std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportWidth)) +
            "x" +
            std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportHeight)));
    }

    void InitializeDx11LightingGeometry()
    {
        VC_ASSERT(m_dx11Device != nullptr, "DX11 lighting geometry initialization requires a ready device.");

        static constexpr std::array<SandboxFullscreenVertex, 4> kFullscreenVertices {
            SandboxFullscreenVertex { .position = { -1.0f, 1.0f }, .uv = { 0.0f, 0.0f } },
            SandboxFullscreenVertex { .position = { 1.0f, 1.0f }, .uv = { 1.0f, 0.0f } },
            SandboxFullscreenVertex { .position = { 1.0f, -1.0f }, .uv = { 1.0f, 1.0f } },
            SandboxFullscreenVertex { .position = { -1.0f, -1.0f }, .uv = { 0.0f, 1.0f } }
        };
        static constexpr std::array<std::uint16_t, 6> kFullscreenIndices { 0, 1, 2, 0, 2, 3 };

        m_dx11LightingGeometry = vc::CreateScope<vc::DX11GeometryBuffer>(
            *m_dx11Device,
            vc::DX11GeometryBufferSpecification {
                .vertexData = kFullscreenVertices.data(),
                .vertexCount = static_cast<std::uint32_t>(kFullscreenVertices.size()),
                .vertexStride = sizeof(SandboxFullscreenVertex),
                .indexData = kFullscreenIndices.data(),
                .indexCount = static_cast<std::uint32_t>(kFullscreenIndices.size()),
                .indexFormat = vc::DX11IndexFormat::UInt16,
                .usage = vc::DX11BufferUsage::Immutable
            });

        const vc::DX11GeometryBufferInfo& geometryInfo = m_dx11LightingGeometry->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 lighting geometry: vertices=" +
            std::to_string(geometryInfo.vertexCount) +
            ", indices=" +
            std::to_string(geometryInfo.indexCount) +
            ", vertexStride=" +
            std::to_string(geometryInfo.vertexStride));
    }

    void InitializeDx11LightingPass()
    {
        VC_ASSERT(m_dx11Device != nullptr, "DX11 lighting pass initialization requires a ready device.");
        VC_ASSERT(m_dx11RenderTargets != nullptr, "DX11 lighting pass initialization requires render targets.");
        VC_ASSERT(m_dx11GBuffer != nullptr, "DX11 lighting pass initialization requires a G-buffer.");

        m_dx11LightingPass = vc::CreateScope<vc::DX11LightingPass>(
            *m_dx11Device,
            *m_dx11RenderTargets,
            *m_dx11GBuffer,
            m_dx11ShadowMap.get());

        const vc::DX11LightingPassInfo& lightingInfo = m_dx11LightingPass->GetInfo();
        const vc::DX11LightingPassParameters& lightingParameters = m_dx11LightingPass->GetParameters();
        VC_LOG_INFO(
            "Sandbox DX11 lighting pass: inputs=" +
            std::to_string(lightingInfo.gBufferInputCount) +
            ", albedo=" +
            lightingInfo.albedoFormat +
            ", normal=" +
            lightingInfo.normalFormat +
            ", material=" +
            lightingInfo.materialFormat +
            ", depth=" +
            lightingInfo.depthFormat +
            ", shadowCascades=" +
            std::to_string(lightingInfo.shadowCascadeCount) +
            ", directionalIntensity=" +
            std::to_string(lightingParameters.directionalLight.intensity) +
            ", pointRadius=" +
            std::to_string(lightingParameters.pointLight.radius));
    }

    void InitializeDx11TransparentPipelineState()
    {
        VC_ASSERT(m_vertexShaderBytecode.IsValid(), "DX11 transparent pipeline initialization requires a compiled vertex shader.");
        VC_ASSERT(
            m_transparentPixelShaderBytecode.IsValid(),
            "DX11 transparent pipeline initialization requires a compiled transparent pixel shader.");
        VC_ASSERT(m_dx11Device != nullptr, "DX11 transparent pipeline initialization requires a ready device.");

        vc::DX11PipelineStateSpecification specification {};
        specification.vertexShader = &m_vertexShaderBytecode;
        specification.pixelShader = &m_transparentPixelShaderBytecode;
        specification.inputElements = {
            vc::DX11InputElement {
                .semanticName = "POSITION",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float3,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, position)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            },
            vc::DX11InputElement {
                .semanticName = "COLOR",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float4,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, color)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            },
            vc::DX11InputElement {
                .semanticName = "TEXCOORD",
                .semanticIndex = 0,
                .format = vc::DX11InputElementFormat::Float2,
                .inputSlot = 0,
                .alignedByteOffset = static_cast<std::uint32_t>(offsetof(SandboxVertex, uv)),
                .perInstanceData = false,
                .instanceDataStepRate = 0
            }
        };
        specification.primitiveTopology = vc::DX11PrimitiveTopology::TriangleList;
        specification.rasterizer.fillMode = vc::DX11FillMode::Solid;
        specification.rasterizer.cullMode = vc::DX11CullMode::None;
        specification.rasterizer.depthClipEnable = true;
        specification.blend.enableBlending = true;
        specification.blend.sourceColor = vc::DX11BlendFactor::SrcAlpha;
        specification.blend.destinationColor = vc::DX11BlendFactor::InvSrcAlpha;
        specification.blend.colorOperation = vc::DX11BlendOperation::Add;
        specification.blend.sourceAlpha = vc::DX11BlendFactor::One;
        specification.blend.destinationAlpha = vc::DX11BlendFactor::InvSrcAlpha;
        specification.blend.alphaOperation = vc::DX11BlendOperation::Add;
        specification.viewport = vc::DX11Viewport {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(GetWindow().GetWidth()),
            .height = static_cast<float>(GetWindow().GetHeight()),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };

        m_dx11TransparentPipelineState = vc::CreateScope<vc::DX11PipelineState>(*m_dx11Device, std::move(specification));
        const vc::DX11PipelineStateInfo& pipelineInfo = m_dx11TransparentPipelineState->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 transparent pipeline: topology=" +
            pipelineInfo.primitiveTopology +
            ", inputElements=" +
            std::to_string(pipelineInfo.inputElementCount) +
            ", rasterizer=" +
            pipelineInfo.fillMode +
            "/" +
            pipelineInfo.cullMode +
            ", blending=" +
            std::string(pipelineInfo.alphaBlendingEnabled ? "true" : "false") +
            ", viewport=" +
            std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportWidth)) +
            "x" +
            std::to_string(static_cast<std::uint32_t>(pipelineInfo.viewportHeight)));
    }

    void InitializeDx11TransparentPass()
    {
        VC_ASSERT(m_dx11Device != nullptr, "DX11 transparent pass initialization requires a ready device.");
        VC_ASSERT(m_dx11RenderTargets != nullptr, "DX11 transparent pass initialization requires render targets.");

        m_dx11TransparentPass = vc::CreateScope<vc::DX11TransparentPass>(*m_dx11Device, *m_dx11RenderTargets);

        const vc::DX11TransparentPassInfo& transparentInfo = m_dx11TransparentPass->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 transparent pass: depth=" +
            transparentInfo.depthFunction +
            ", depthWrites=" +
            std::string(transparentInfo.depthWritesEnabled ? "true" : "false") +
            ", alphaBlendRequired=" +
            std::string(transparentInfo.alphaBlendingRequired ? "true" : "false"));
    }

    void UpdateDx11ShadowPassData(std::uint64_t nextFrame)
    {
        const vc::DX11DirectionalLightParameters directionalLight = BuildSandboxDirectionalLightParameters(nextFrame);
        const vc::Vec3 lightDirection(
            directionalLight.direction[0],
            directionalLight.direction[1],
            directionalLight.direction[2]);
        m_shadowCascadeStates = BuildSandboxShadowCascadeStates(lightDirection);
    }

    void UpdateDx11LightingPassParameters(std::uint64_t nextFrame)
    {
        if (!m_dx11LightingPass)
        {
            return;
        }

        const float time = static_cast<float>(nextFrame) * 0.01f;
        vc::DX11LightingPassParameters parameters = m_dx11LightingPass->GetParameters();
        parameters.aspectRatio = GetWindowAspectRatio();
        parameters.directionalLight = BuildSandboxDirectionalLightParameters(nextFrame);
        parameters.pointLight.screenUv = {
            0.50f + 0.18f * std::sin(time * 0.83f + 0.25f),
            0.48f + 0.16f * std::cos(time * 0.57f + 0.4f)
        };
        parameters.pointLight.radius = 0.24f + 0.06f * (0.5f + 0.5f * std::sin(time * 0.49f + 0.6f));
        parameters.pointLight.intensity = 1.25f + 0.25f * std::sin(time * 0.65f + 0.35f);
        parameters.pointLight.color = { 1.00f, 0.46f, 0.23f };
        parameters.pointLight.depthInfluence = 0.35f;
        parameters.normalStrength = 1.0f;
        parameters.materialInfluence = 0.45f;
        parameters.shadows.enabled = m_dx11ShadowMap ? 1.0f : 0.0f;
        parameters.shadows.strength = 0.78f;
        parameters.shadows.depthBias = 0.0018f;
        parameters.shadows.cascadeBlend = 0.0f;
        parameters.shadows.cameraPosition = { 0.0f, 0.0f, 0.0f };
        parameters.shadows.cameraForward = { 0.0f, 0.0f, 1.0f };
        parameters.shadows.cascadeCount = m_dx11ShadowMap ? m_dx11ShadowMap->GetInfo().cascadeCount : 0u;
        for (std::size_t cascadeIndex = 0; cascadeIndex < m_shadowCascadeStates.size(); ++cascadeIndex)
        {
            parameters.shadows.cascades[cascadeIndex].worldToShadowTextureMatrix
                = m_shadowCascadeStates[cascadeIndex].worldToShadowTexture;
            parameters.shadows.cascades[cascadeIndex].splitDistance
                = m_shadowCascadeStates[cascadeIndex].splitDistance;
        }
        m_dx11LightingPass->UpdateParameters(std::move(parameters));
    }

    void InitializeDx11BootstrapTexture()
    {
        VC_ASSERT(m_dx11Device != nullptr, "DX11 texture initialization requires a ready device.");

        const vc::Path texturePath = ResolveSandboxTexturePath();
        const bool textureAssetCreated = EnsureSandboxTextureAsset(texturePath);
        if (textureAssetCreated)
        {
            VC_LOG_INFO("Generated sandbox DDS texture asset: " + texturePath.string());
        }

        m_dx11BootstrapTexture = vc::CreateScope<vc::DX11Texture2D>(
            *m_dx11Device,
            vc::DX11Texture2DSpecification {
                .path = texturePath
            });

        const vc::DX11Texture2DInfo& textureInfo = m_dx11BootstrapTexture->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 texture: size=" +
            std::to_string(textureInfo.width) +
            "x" +
            std::to_string(textureInfo.height) +
            ", mips=" +
            std::to_string(textureInfo.mipLevels) +
            ", format=" +
            textureInfo.format +
            ", path=" +
            textureInfo.sourcePath.string());
    }

    void InitializeDx11TextureAtlas()
    {
        VC_ASSERT(m_dx11BootstrapTexture != nullptr, "DX11 texture atlas initialization requires a ready texture.");

        const vc::DX11Texture2DInfo& textureInfo = m_dx11BootstrapTexture->GetInfo();
        VC_ASSERT(textureInfo.width >= 2, "Sandbox texture atlas expects at least a 2-pixel-wide texture.");
        VC_ASSERT(textureInfo.height >= 2, "Sandbox texture atlas expects at least a 2-pixel-tall texture.");

        m_dx11TextureAtlas = vc::CreateScope<vc::DX11TextureAtlas>(
            *m_dx11BootstrapTexture,
            vc::DX11TextureAtlasSpecification {
                .buildUniformGrid = true,
                .tileWidth = std::max(1u, textureInfo.width / 2u),
                .tileHeight = std::max(1u, textureInfo.height / 2u),
                .paddingX = 0,
                .paddingY = 0,
                .marginX = 0,
                .marginY = 0,
                .regionNamePrefix = "bootstrap-tile"
            });

        const vc::DX11TextureAtlasInfo& atlasInfo = m_dx11TextureAtlas->GetInfo();
        VC_LOG_INFO(
            "Sandbox DX11 texture atlas: regions=" +
            std::to_string(atlasInfo.regionCount) +
            ", textureSize=" +
            std::to_string(atlasInfo.textureWidth) +
            "x" +
            std::to_string(atlasInfo.textureHeight));
        for (const vc::DX11TextureAtlasRegion& region : m_dx11TextureAtlas->GetRegions())
        {
            VC_LOG_INFO(
                "Atlas region [" +
                region.name +
                "]: px=(" +
                std::to_string(region.pixelX) +
                ", " +
                std::to_string(region.pixelY) +
                ", " +
                std::to_string(region.pixelWidth) +
                ", " +
                std::to_string(region.pixelHeight) +
                "), uv=" +
                FormatUvRect(region.uMin, region.vMin, region.uMax, region.vMax));
        }
    }

    [[nodiscard]] const SandboxAtlasTriangleVariant* GetActiveDx11AtlasVariant() const noexcept
    {
        if (m_dx11AtlasTriangleVariants.empty())
        {
            return nullptr;
        }

        const std::size_t clampedIndex = std::min(m_activeDx11AtlasVariantIndex, m_dx11AtlasTriangleVariants.size() - 1);
        return &m_dx11AtlasTriangleVariants[clampedIndex];
    }

    [[nodiscard]] std::uint64_t GetDx11AtlasGeometryBindCount() const noexcept
    {
        std::uint64_t bindCount = 0;
        for (const SandboxAtlasTriangleVariant& variant : m_dx11AtlasTriangleVariants)
        {
            bindCount += variant.geometry ? variant.geometry->GetInfo().bindCount : 0;
        }

        return bindCount;
    }

    [[nodiscard]] std::uint64_t GetDx11AtlasGeometryDrawCount() const noexcept
    {
        std::uint64_t drawCount = 0;
        for (const SandboxAtlasTriangleVariant& variant : m_dx11AtlasTriangleVariants)
        {
            drawCount += variant.geometry ? variant.geometry->GetInfo().drawCount : 0;
        }

        return drawCount;
    }

    [[nodiscard]] std::uint64_t GetDx11TransparentGeometryBindCount() const noexcept
    {
        std::uint64_t bindCount = 0;
        for (const SandboxAtlasTriangleVariant& variant : m_dx11AtlasTriangleVariants)
        {
            bindCount += variant.transparentGeometry ? variant.transparentGeometry->GetInfo().bindCount : 0;
        }

        return bindCount;
    }

    [[nodiscard]] std::uint64_t GetDx11TransparentGeometryDrawCount() const noexcept
    {
        std::uint64_t drawCount = 0;
        for (const SandboxAtlasTriangleVariant& variant : m_dx11AtlasTriangleVariants)
        {
            drawCount += variant.transparentGeometry ? variant.transparentGeometry->GetInfo().drawCount : 0;
        }

        return drawCount;
    }

    void UpdateActiveDx11AtlasVariant(std::uint64_t nextFrame)
    {
        if (m_dx11AtlasTriangleVariants.empty())
        {
            return;
        }

        const std::uint64_t configuredFrameBudget = GetSpecification().maxFrames > 0 ? GetSpecification().maxFrames : 240;
        const std::uint64_t framesPerVariant = std::max<std::uint64_t>(1, configuredFrameBudget / m_dx11AtlasTriangleVariants.size());
        const std::size_t nextVariantIndex = static_cast<std::size_t>(((nextFrame - 1u) / framesPerVariant) % m_dx11AtlasTriangleVariants.size());
        if (nextVariantIndex == m_activeDx11AtlasVariantIndex && nextFrame > 1)
        {
            return;
        }

        m_activeDx11AtlasVariantIndex = nextVariantIndex;
        const SandboxAtlasTriangleVariant& activeVariant = m_dx11AtlasTriangleVariants[m_activeDx11AtlasVariantIndex];
        VC_LOG_INFO(
            "Atlas region switched: " +
            activeVariant.regionName +
            ", uv=" +
            activeVariant.uvSummary +
            ", frame=" +
            std::to_string(nextFrame));
    }

    void SyncPrimaryCameraAspectRatio(std::uint32_t width, std::uint32_t height, bool emitLog)
    {
        if (!m_registry.IsValid(m_cameraEntityId))
        {
            return;
        }

        vc::Entity cameraEntity = m_registry.Wrap(m_cameraEntityId);
        if (!cameraEntity || !cameraEntity.HasComponent<vc::CameraComponent>())
        {
            return;
        }

        vc::CameraComponent& camera = cameraEntity.GetComponent<vc::CameraComponent>();
        const float previousAspectRatio = camera.aspectRatio;
        camera.SetAspectRatioFromViewport(width, height);
        if (emitLog && std::fabs(previousAspectRatio - camera.aspectRatio) > 1.0e-4f)
        {
            VC_LOG_INFO(
                "Camera aspect updated: " +
                std::to_string(previousAspectRatio) +
                " -> " +
                std::to_string(camera.aspectRatio));
        }
    }

    void RefreshDx11BufferReadback(std::uint32_t frameIndex, std::uint32_t clearCount)
    {
        if (!m_dx11DynamicBuffer || !m_dx11ReadbackBuffer || !m_dx11ContextSync)
        {
            return;
        }

        SandboxGpuFrameBlock frameBlock {};
        frameBlock.frameIndex = frameIndex;
        frameBlock.clearCount = clearCount;
        frameBlock.aspectRatioMilli = QuantizeAspectRatio(GetWindowAspectRatio());
        frameBlock.checksum = ComputeGpuFrameChecksum(frameBlock);

        m_dx11DynamicBuffer->Write(&frameBlock, sizeof(frameBlock));
        m_dx11ReadbackBuffer->CopyFrom(*m_dx11DynamicBuffer);

        const bool gpuIdle = m_dx11ContextSync->WaitForGpuIdle(2000);
        VC_ASSERT(gpuIdle, "Failed to reach GPU idle while reading back DX11 buffer contents.");

        const vc::DX11MappedBuffer mappedBuffer = m_dx11ReadbackBuffer->Map(vc::DX11BufferMapMode::Read);
        VC_ASSERT(mappedBuffer.data != nullptr, "DX11 readback buffer returned a null mapped pointer.");
        std::memcpy(&m_lastGpuFrameBlock, mappedBuffer.data, sizeof(m_lastGpuFrameBlock));
        m_dx11ReadbackBuffer->Unmap();

        m_lastGpuFrameBlockValid = (m_lastGpuFrameBlock.checksum == ComputeGpuFrameChecksum(m_lastGpuFrameBlock));
        VC_ASSERT(m_lastGpuFrameBlockValid, "DX11 buffer readback checksum validation failed.");
    }

    vc::Scope<vc::FileSystemWatcher> m_configWatcher;
    vc::Registry m_registry;
    vc::SystemScheduler m_systemScheduler;
    vc::Scope<vc::DX11Buffer> m_dx11DynamicBuffer;
    vc::Scope<vc::DX11Buffer> m_dx11ReadbackBuffer;
    vc::Scope<vc::DX11Buffer> m_dx11ShadowPassBuffer;
    vc::Scope<vc::DX11Texture2D> m_dx11BootstrapTexture;
    vc::Scope<vc::DX11TextureAtlas> m_dx11TextureAtlas;
    vc::Scope<vc::DX11PipelineState> m_dx11PipelineState;
    vc::Scope<vc::DX11PipelineState> m_dx11ShadowPipelineState;
    vc::Scope<vc::DX11PipelineState> m_dx11LightingPipelineState;
    vc::Scope<vc::DX11PipelineState> m_dx11TransparentPipelineState;
    vc::Scope<vc::DX11DepthPrePass> m_dx11DepthPrePass;
    vc::Scope<vc::DX11GBuffer> m_dx11GBuffer;
    vc::Scope<vc::DX11LightingPass> m_dx11LightingPass;
    vc::Scope<vc::DX11TransparentPass> m_dx11TransparentPass;
    vc::Scope<vc::DX11GeometryBuffer> m_dx11LightingGeometry;
    vc::Scope<vc::DX11ContextSync> m_dx11ContextSync;
    vc::Scope<vc::DX11Device> m_dx11Device;
    vc::Scope<vc::DX11RenderTargets> m_dx11RenderTargets;
    vc::Scope<vc::DX11ShadowMap> m_dx11ShadowMap;
    vc::Scope<vc::DX11SwapChain> m_dx11SwapChain;
    std::vector<SandboxAtlasTriangleVariant> m_dx11AtlasTriangleVariants;
    std::array<SandboxShadowCascadeState, vc::DX11ShadowCascadeCount> m_shadowCascadeStates {};
    vc::Scope<vc::ArenaAllocator> m_bootstrapArena;
    vc::Scope<vc::PoolAllocator> m_particlePool;
    SandboxPoolParticle* m_retainedParticle = nullptr;
    vc::JobHandle m_primeJob;
    vc::EntityId m_playerEntityId = vc::NullEntity;
    vc::EntityId m_cameraEntityId = vc::NullEntity;
    std::size_t m_activeDx11AtlasVariantIndex = 0;
    vc::DX11ShaderBytecode m_vertexShaderBytecode;
    vc::DX11ShaderBytecode m_pixelShaderBytecode;
    vc::DX11ShaderBytecode m_shadowVertexShaderBytecode;
    vc::DX11ShaderBytecode m_shadowPixelShaderBytecode;
    vc::DX11ShaderBytecode m_lightingVertexShaderBytecode;
    vc::DX11ShaderBytecode m_lightingPixelShaderBytecode;
    vc::DX11ShaderBytecode m_transparentPixelShaderBytecode;
    SandboxGpuFrameBlock m_lastGpuFrameBlock {};
    std::uint32_t m_primeCountResult = 0;
    std::uint32_t m_shaderCompileCount = 0;
    std::uint64_t m_shaderCompileByteSize = 0;
    bool m_lastGpuFrameBlockValid = false;
    bool m_shaderCompilerSmokeTestPassed = false;
    bool m_primeJobReported = false;
};

int main()
{
    SandboxApplication application;
    application.Run();
    return 0;
}
