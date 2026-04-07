#include <VoxelCube/Core/Input.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <Windows.h>
#include <windowsx.h>

#include <VoxelCube/Core/Base.hpp>
#include <VoxelCube/Core/Events.hpp>
#include <VoxelCube/Core/Log.hpp>

#include "InputInternal.hpp"

namespace
{
    constexpr std::size_t kKeyCount = 256;
    constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(vc::MouseButton::Count);

    struct InputState
    {
        HWND windowHandle = nullptr;
        bool initialized = false;

        std::array<bool, kKeyCount> keyDown {};
        std::array<bool, kKeyCount> keyPressed {};
        std::array<bool, kKeyCount> keyReleased {};

        std::array<bool, kMouseButtonCount> mouseDown {};
        std::array<bool, kMouseButtonCount> mousePressed {};
        std::array<bool, kMouseButtonCount> mouseReleased {};

        std::int32_t mouseX = 0;
        std::int32_t mouseY = 0;
        std::int32_t mouseDeltaX = 0;
        std::int32_t mouseDeltaY = 0;
        float mouseWheelDelta = 0.0f;
        bool hasRawMouseDelta = false;
    };

    InputState& GetInputBackendState()
    {
        static InputState state;
        return state;
    }

    constexpr std::size_t ToIndex(vc::KeyCode key)
    {
        return static_cast<std::size_t>(key);
    }

    constexpr std::size_t ToIndex(vc::MouseButton button)
    {
        return static_cast<std::size_t>(button);
    }

    void SetKeyState(InputState& state, std::uintptr_t virtualKey, bool pressed)
    {
        if (virtualKey >= kKeyCount)
        {
            return;
        }

        const auto index = static_cast<std::size_t>(virtualKey);
        if (pressed)
        {
            if (!state.keyDown[index])
            {
                state.keyPressed[index] = true;
            }
        }
        else if (state.keyDown[index])
        {
            state.keyReleased[index] = true;
        }

        state.keyDown[index] = pressed;
    }

    void SetMouseButtonState(InputState& state, vc::MouseButton button, bool pressed)
    {
        const auto index = ToIndex(button);
        if (index >= kMouseButtonCount)
        {
            return;
        }

        if (pressed)
        {
            if (!state.mouseDown[index])
            {
                state.mousePressed[index] = true;
            }
        }
        else if (state.mouseDown[index])
        {
            state.mouseReleased[index] = true;
        }

        state.mouseDown[index] = pressed;
    }

    void ResetFrameState(InputState& state)
    {
        state.keyPressed.fill(false);
        state.keyReleased.fill(false);
        state.mousePressed.fill(false);
        state.mouseReleased.fill(false);
        state.mouseDeltaX = 0;
        state.mouseDeltaY = 0;
        state.mouseWheelDelta = 0.0f;
        state.hasRawMouseDelta = false;
    }

    void ResetAllState(InputState& state)
    {
        state.keyDown.fill(false);
        state.keyPressed.fill(false);
        state.keyReleased.fill(false);
        state.mouseDown.fill(false);
        state.mousePressed.fill(false);
        state.mouseReleased.fill(false);
        state.mouseDeltaX = 0;
        state.mouseDeltaY = 0;
        state.mouseWheelDelta = 0.0f;
        state.hasRawMouseDelta = false;
    }

    void UpdateMousePosition(InputState& state, LPARAM lParam)
    {
        state.mouseX = GET_X_LPARAM(lParam);
        state.mouseY = GET_Y_LPARAM(lParam);
    }

    void HandleRawInput(InputState& state, LPARAM lParam)
    {
        UINT dataSize = 0;
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize, sizeof(RAWINPUTHEADER)) != 0)
        {
            return;
        }

        if (dataSize == 0)
        {
            return;
        }

        std::vector<std::byte> rawStorage(dataSize);
        if (GetRawInputData(
                reinterpret_cast<HRAWINPUT>(lParam),
                RID_INPUT,
                rawStorage.data(),
                &dataSize,
                sizeof(RAWINPUTHEADER)) != dataSize)
        {
            return;
        }

        const auto* rawInput = reinterpret_cast<const RAWINPUT*>(rawStorage.data());
        if (rawInput->header.dwType != RIM_TYPEMOUSE)
        {
            return;
        }

        state.mouseDeltaX += rawInput->data.mouse.lLastX;
        state.mouseDeltaY += rawInput->data.mouse.lLastY;
        state.hasRawMouseDelta = state.hasRawMouseDelta || rawInput->data.mouse.lLastX != 0 || rawInput->data.mouse.lLastY != 0;

        if (rawInput->data.mouse.lLastX != 0 || rawInput->data.mouse.lLastY != 0)
        {
            vc::detail::EmitActiveEvent<vc::MouseRawDeltaEvent>(rawInput->data.mouse.lLastX, rawInput->data.mouse.lLastY);
        }
    }

    void RegisterRawDevices(HWND windowHandle)
    {
        RAWINPUTDEVICE devices[2] {};
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x02;
        devices[0].dwFlags = 0;
        devices[0].hwndTarget = windowHandle;

        devices[1].usUsagePage = 0x01;
        devices[1].usUsage = 0x06;
        devices[1].dwFlags = 0;
        devices[1].hwndTarget = windowHandle;

        const BOOL result = RegisterRawInputDevices(devices, static_cast<UINT>(std::size(devices)), sizeof(RAWINPUTDEVICE));
        VC_ASSERT(result == TRUE, "Failed to register raw input devices.");
    }
}

namespace vc
{
    void Input::Initialize(void* nativeWindowHandle)
    {
        auto& state = GetInputBackendState();
        state.windowHandle = static_cast<HWND>(nativeWindowHandle);
        VC_ASSERT(state.windowHandle != nullptr, "Input requires a valid native window handle.");

        ResetAllState(state);
        RegisterRawDevices(state.windowHandle);
        state.initialized = true;

        VC_LOG_INFO("Input system initialized (keyboard, mouse, raw input).");
    }

    void Input::Shutdown()
    {
        auto& state = GetInputBackendState();
        ResetAllState(state);
        state.windowHandle = nullptr;
        state.initialized = false;
    }

    void Input::BeginFrame()
    {
        auto& state = GetInputBackendState();
        if (!state.initialized)
        {
            return;
        }

        ResetFrameState(state);
    }

    bool Input::IsKeyDown(KeyCode key)
    {
        const auto& state = GetInputBackendState();
        return state.keyDown[ToIndex(key)];
    }

    bool Input::WasKeyPressed(KeyCode key)
    {
        const auto& state = GetInputBackendState();
        return state.keyPressed[ToIndex(key)];
    }

    bool Input::WasKeyReleased(KeyCode key)
    {
        const auto& state = GetInputBackendState();
        return state.keyReleased[ToIndex(key)];
    }

    bool Input::IsMouseButtonDown(MouseButton button)
    {
        const auto& state = GetInputBackendState();
        return state.mouseDown[ToIndex(button)];
    }

    bool Input::WasMouseButtonPressed(MouseButton button)
    {
        const auto& state = GetInputBackendState();
        return state.mousePressed[ToIndex(button)];
    }

    bool Input::WasMouseButtonReleased(MouseButton button)
    {
        const auto& state = GetInputBackendState();
        return state.mouseReleased[ToIndex(button)];
    }

    std::int32_t Input::GetMouseX()
    {
        return GetInputBackendState().mouseX;
    }

    std::int32_t Input::GetMouseY()
    {
        return GetInputBackendState().mouseY;
    }

    std::int32_t Input::GetMouseDeltaX()
    {
        return GetInputBackendState().mouseDeltaX;
    }

    std::int32_t Input::GetMouseDeltaY()
    {
        return GetInputBackendState().mouseDeltaY;
    }

    float Input::GetMouseWheelDelta()
    {
        return GetInputBackendState().mouseWheelDelta;
    }

    bool Input::HasRawMouseDelta()
    {
        return GetInputBackendState().hasRawMouseDelta;
    }
}

namespace vc::detail
{
    void HandleInputWindowMessage(std::uintptr_t message, std::uintptr_t wParam, std::intptr_t lParam)
    {
        auto& state = GetInputBackendState();
        if (!state.initialized)
        {
            return;
        }

        switch (message)
        {
        case WM_INPUT:
            HandleRawInput(state, static_cast<LPARAM>(lParam));
            break;

        case WM_MOUSEMOVE:
            UpdateMousePosition(state, static_cast<LPARAM>(lParam));
            detail::EmitActiveEvent<MouseMovedEvent>(state.mouseX, state.mouseY);
            break;

        case WM_MOUSEWHEEL:
        {
            const float mouseWheelStep =
                static_cast<float>(GET_WHEEL_DELTA_WPARAM(static_cast<WPARAM>(wParam))) / static_cast<float>(WHEEL_DELTA);
            state.mouseWheelDelta += mouseWheelStep;
            detail::EmitActiveEvent<MouseScrolledEvent>(mouseWheelStep);
            break;
        }

        case WM_LBUTTONDOWN:
            SetMouseButtonState(state, MouseButton::Left, true);
            detail::EmitActiveEvent<MouseButtonPressedEvent>(MouseButton::Left);
            break;

        case WM_LBUTTONUP:
            SetMouseButtonState(state, MouseButton::Left, false);
            detail::EmitActiveEvent<MouseButtonReleasedEvent>(MouseButton::Left);
            break;

        case WM_RBUTTONDOWN:
            SetMouseButtonState(state, MouseButton::Right, true);
            detail::EmitActiveEvent<MouseButtonPressedEvent>(MouseButton::Right);
            break;

        case WM_RBUTTONUP:
            SetMouseButtonState(state, MouseButton::Right, false);
            detail::EmitActiveEvent<MouseButtonReleasedEvent>(MouseButton::Right);
            break;

        case WM_MBUTTONDOWN:
            SetMouseButtonState(state, MouseButton::Middle, true);
            detail::EmitActiveEvent<MouseButtonPressedEvent>(MouseButton::Middle);
            break;

        case WM_MBUTTONUP:
            SetMouseButtonState(state, MouseButton::Middle, false);
            detail::EmitActiveEvent<MouseButtonReleasedEvent>(MouseButton::Middle);
            break;

        case WM_XBUTTONDOWN:
        {
            const MouseButton button =
                GET_XBUTTON_WPARAM(static_cast<WPARAM>(wParam)) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
            SetMouseButtonState(
                state,
                button,
                true);
            detail::EmitActiveEvent<MouseButtonPressedEvent>(button);
            break;
        }

        case WM_XBUTTONUP:
        {
            const MouseButton button =
                GET_XBUTTON_WPARAM(static_cast<WPARAM>(wParam)) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
            SetMouseButtonState(
                state,
                button,
                false);
            detail::EmitActiveEvent<MouseButtonReleasedEvent>(button);
            break;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            SetKeyState(state, wParam, true);
            detail::EmitActiveEvent<KeyPressedEvent>(
                static_cast<KeyCode>(wParam),
                (static_cast<std::uint32_t>(lParam) & (1u << 30)) != 0);
            break;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            SetKeyState(state, wParam, false);
            detail::EmitActiveEvent<KeyReleasedEvent>(static_cast<KeyCode>(wParam));
            break;

        case WM_KILLFOCUS:
            ResetAllState(state);
            break;
        }
    }
}
