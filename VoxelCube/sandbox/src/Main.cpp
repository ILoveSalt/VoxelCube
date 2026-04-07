#include <exception>
#include <iostream>
#include <string>

#include <VoxelCube/VoxelCube.hpp>

namespace
{
    vc::ApplicationSpecification GetDefaultSandboxSpecification()
    {
        return {
            .name = "VoxelCube Sandbox",
            .windowWidth = 1600,
            .windowHeight = 900,
            .windowResizable = true,
            .startMaximized = false,
            .startFullscreen = false,
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
    }

    void OnFixedUpdate(vc::Timestep fixedTimeStep) override
    {
        const auto nextTick = GetFixedTickCount();
        if (nextTick > 0 && nextTick % 60 == 0)
        {
            VC_LOG_TRACE("Fixed tick " + std::to_string(nextTick) + " dt=" + std::to_string(fixedTimeStep.GetMilliseconds()) + "ms");
        }
    }

    void OnShutdown() override
    {
        m_configWatcher.reset();
        VC_LOG_INFO("Sandbox shutdown hook executed.");
    }

private:
    vc::Scope<vc::FileSystemWatcher> m_configWatcher;
};

int main()
{
    SandboxApplication application;
    application.Run();
    return 0;
}
