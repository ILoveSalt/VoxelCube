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

    struct SandboxAtlasTriangleVariant
    {
        std::string regionName;
        std::string uvSummary;
        vc::Scope<vc::DX11GeometryBuffer> geometry;
    };

    struct SandboxGpuFrameBlock
    {
        std::uint32_t frameIndex = 0;
        std::uint32_t clearCount = 0;
        std::uint32_t aspectRatioMilli = 0;
        std::uint32_t checksum = 0;
    };

    static_assert(sizeof(SandboxGpuFrameBlock) == 16, "SandboxGpuFrameBlock must stay 16 bytes.");

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
        InitializeDx11PipelineState();
        InitializeDx11BootstrapTexture();
        InitializeDx11TextureAtlas();
        InitializeDx11Geometry();

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
            m_dx11RenderTargets->Bind();
            m_dx11RenderTargets->Clear(clearColor);
            if (m_dx11PipelineState)
            {
                m_dx11PipelineState->Bind();
            }
            if (m_dx11TextureAtlas)
            {
                m_dx11TextureAtlas->BindPS(0);
            }
            if (const SandboxAtlasTriangleVariant* activeVariant = GetActiveDx11AtlasVariant())
            {
                activeVariant->geometry->Bind();
                activeVariant->geometry->DrawIndexed();
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
                    ", readback frame=" +
                    std::to_string(m_lastGpuFrameBlock.frameIndex) +
                    ", checksumValid=" +
                    std::string(m_lastGpuFrameBlockValid ? "true" : "false") +
                    ", pipeline binds=" +
                    std::to_string(m_dx11PipelineState ? m_dx11PipelineState->GetInfo().bindCount : 0) +
                    ", atlas binds=" +
                    std::to_string(m_dx11TextureAtlas ? m_dx11TextureAtlas->GetInfo().bindCount : 0) +
                    ", texture binds=" +
                    std::to_string(m_dx11BootstrapTexture ? m_dx11BootstrapTexture->GetInfo().bindCount : 0) +
                    ", atlas region=" +
                    std::string(GetActiveDx11AtlasVariant() != nullptr ? GetActiveDx11AtlasVariant()->regionName : "none") +
                    ", geometry binds=" +
                    std::to_string(GetDx11AtlasGeometryBindCount()) +
                    ", geometry draws=" +
                    std::to_string(GetDx11AtlasGeometryDrawCount()) +
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
                "DX11 pipeline stats before shutdown: binds=" +
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
        m_dx11TextureAtlas.reset();
        m_dx11BootstrapTexture.reset();
        m_dx11PipelineState.reset();
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

        vc::DX11ShaderCompileSpecification pixelSpecification {};
        pixelSpecification.entryPoint = "SandboxPixelMain";
        pixelSpecification.stage = vc::DX11ShaderStage::Pixel;
        m_pixelShaderBytecode = vc::DX11ShaderCompiler::CompileFromFile(shaderPath, std::move(pixelSpecification));

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
        VC_ASSERT(inlinePixelShader.IsValid(), "Sandbox inline pixel shader bytecode is invalid.");
        VC_ASSERT(m_vertexShaderBytecode.GetData() != nullptr, "Sandbox vertex shader bytecode pointer is null.");
        VC_ASSERT(m_pixelShaderBytecode.GetData() != nullptr, "Sandbox pixel shader bytecode pointer is null.");
        VC_ASSERT(inlinePixelShader.GetData() != nullptr, "Sandbox inline pixel shader bytecode pointer is null.");

        m_shaderCompileCount = 3;
        m_shaderCompileByteSize = m_vertexShaderBytecode.GetInfo().sizeInBytes
            + m_pixelShaderBytecode.GetInfo().sizeInBytes
            + inlinePixelShader.GetInfo().sizeInBytes;
        m_shaderCompilerSmokeTestPassed = true;

        VC_LOG_INFO(
            "Sandbox HLSL compile: fileVS=" +
            std::to_string(m_vertexShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, filePS=" +
            std::to_string(m_pixelShaderBytecode.GetInfo().sizeInBytes) +
            " bytes, inlinePS=" +
            std::to_string(inlinePixelShader.GetInfo().sizeInBytes) +
            " bytes, total=" +
            std::to_string(m_shaderCompileByteSize) +
            ", warnings=" +
            std::string(
                m_vertexShaderBytecode.GetInfo().hasWarnings
                        || m_pixelShaderBytecode.GetInfo().hasWarnings
                        || inlinePixelShader.GetInfo().hasWarnings
                    ? "true"
                    : "false") +
            ", file=" +
            shaderPath.string());
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
        specification.blend.enableBlending = true;
        specification.blend.sourceColor = vc::DX11BlendFactor::SrcAlpha;
        specification.blend.destinationColor = vc::DX11BlendFactor::InvSrcAlpha;
        specification.blend.sourceAlpha = vc::DX11BlendFactor::One;
        specification.blend.destinationAlpha = vc::DX11BlendFactor::InvSrcAlpha;
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

        static constexpr std::array<std::uint16_t, 3> kTriangleIndices { 0, 1, 2 };
        m_dx11AtlasTriangleVariants.clear();

        for (const vc::DX11TextureAtlasRegion& region : m_dx11TextureAtlas->GetRegions())
        {
            const std::array<SandboxVertex, 3> triangleVertices {
                SandboxVertex {
                    .position = { 0.0f, 0.60f, 0.0f },
                    .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                    .uv = { region.uCenter, region.vMin }
                },
                SandboxVertex {
                    .position = { 0.58f, -0.42f, 0.0f },
                    .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                    .uv = { region.uMax, region.vMax }
                },
                SandboxVertex {
                    .position = { -0.58f, -0.42f, 0.0f },
                    .color = { 1.0f, 1.0f, 1.0f, 1.0f },
                    .uv = { region.uMin, region.vMax }
                }
            };

            SandboxAtlasTriangleVariant variant {};
            variant.regionName = region.name;
            variant.uvSummary = FormatUvRect(region.uMin, region.vMin, region.uMax, region.vMax);
            variant.geometry = vc::CreateScope<vc::DX11GeometryBuffer>(
                *m_dx11Device,
                vc::DX11GeometryBufferSpecification {
                    .vertexData = triangleVertices.data(),
                    .vertexCount = static_cast<std::uint32_t>(triangleVertices.size()),
                    .vertexStride = sizeof(SandboxVertex),
                    .indexData = kTriangleIndices.data(),
                    .indexCount = static_cast<std::uint32_t>(kTriangleIndices.size()),
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
            ", vertexStride=" +
            std::to_string(geometryInfo.vertexStride) +
            ", indices=" +
            std::to_string(geometryInfo.indexCount));
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
    vc::Scope<vc::DX11Texture2D> m_dx11BootstrapTexture;
    vc::Scope<vc::DX11TextureAtlas> m_dx11TextureAtlas;
    vc::Scope<vc::DX11PipelineState> m_dx11PipelineState;
    vc::Scope<vc::DX11ContextSync> m_dx11ContextSync;
    vc::Scope<vc::DX11Device> m_dx11Device;
    vc::Scope<vc::DX11RenderTargets> m_dx11RenderTargets;
    vc::Scope<vc::DX11SwapChain> m_dx11SwapChain;
    std::vector<SandboxAtlasTriangleVariant> m_dx11AtlasTriangleVariants;
    vc::Scope<vc::ArenaAllocator> m_bootstrapArena;
    vc::Scope<vc::PoolAllocator> m_particlePool;
    SandboxPoolParticle* m_retainedParticle = nullptr;
    vc::JobHandle m_primeJob;
    vc::EntityId m_playerEntityId = vc::NullEntity;
    vc::EntityId m_cameraEntityId = vc::NullEntity;
    std::size_t m_activeDx11AtlasVariantIndex = 0;
    vc::DX11ShaderBytecode m_vertexShaderBytecode;
    vc::DX11ShaderBytecode m_pixelShaderBytecode;
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
