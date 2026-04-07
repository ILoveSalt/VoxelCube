#include <VoxelCube/Core/Window.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

#include <Windows.h>

#include <VoxelCube/Core/Events.hpp>
#include <VoxelCube/Core/Log.hpp>

#include "InputInternal.hpp"

namespace
{
    constexpr wchar_t kWindowClassName[] = L"VoxelCubeWindowClass";

    std::wstring Utf8ToWide(std::string_view value)
    {
        if (value.empty())
        {
            return {};
        }

        const int requiredCharacters = MultiByteToWideChar(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);

        VC_ASSERT(requiredCharacters > 0, "Failed to convert UTF-8 text to UTF-16.");

        std::wstring wideText(static_cast<std::size_t>(requiredCharacters), L'\0');
        const int writtenCharacters = MultiByteToWideChar(
            CP_UTF8,
            0,
            value.data(),
            static_cast<int>(value.size()),
            wideText.data(),
            requiredCharacters);

        VC_ASSERT(writtenCharacters == requiredCharacters, "Failed to write UTF-16 text.");
        return wideText;
    }

}

namespace vc
{
    class Window::Impl
    {
    public:
        explicit Impl(WindowSpecification specification)
            : m_specification(std::move(specification))
            , m_instance(GetModuleHandleW(nullptr))
            , m_wideTitle(Utf8ToWide(m_specification.title))
        {
            VC_ASSERT(m_instance != nullptr, "Failed to get module handle for the current process.");
            VC_ASSERT(m_specification.width > 0, "Window width must be greater than zero.");
            VC_ASSERT(m_specification.height > 0, "Window height must be greater than zero.");

            CreateNativeWindow();
            ShowNativeWindow();

            VC_LOG_INFO("Win32 window created: " + m_specification.title + " (" + std::to_string(m_width) + "x" + std::to_string(m_height) + ")");
        }

        ~Impl()
        {
            Close();
        }

        Impl(Impl&&) = delete;
        Impl& operator=(Impl&&) = delete;

        bool ProcessMessages()
        {
            MSG message {};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    m_shouldClose = true;
                    return false;
                }

                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            return !m_shouldClose;
        }

        void Close()
        {
            if (m_handle == nullptr)
            {
                return;
            }

            m_shouldClose = true;
            detail::EmitActiveEvent<WindowCloseEvent>();
            DestroyWindow(m_handle);
            m_handle = nullptr;
        }

        void SetTitle(const std::string& title)
        {
            m_specification.title = title;
            m_wideTitle = Utf8ToWide(title);
            SetWindowTextW(m_handle, m_wideTitle.c_str());
        }

        void ToggleFullscreen()
        {
            SetFullscreen(!m_fullscreen);
        }

        void SetFullscreen(bool enabled)
        {
            if (m_handle == nullptr || enabled == m_fullscreen)
            {
                return;
            }

            if (enabled)
            {
                m_windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(m_handle, GWL_STYLE));
                m_windowedExStyle = static_cast<DWORD>(GetWindowLongPtrW(m_handle, GWL_EXSTYLE));
                GetWindowRect(m_handle, &m_windowedRect);

                MONITORINFO monitorInfo {};
                monitorInfo.cbSize = sizeof(monitorInfo);
                GetMonitorInfoW(MonitorFromWindow(m_handle, MONITOR_DEFAULTTONEAREST), &monitorInfo);

                SetWindowLongPtrW(m_handle, GWL_STYLE, m_windowedStyle & ~WS_OVERLAPPEDWINDOW);
                SetWindowPos(
                    m_handle,
                    HWND_TOP,
                    monitorInfo.rcMonitor.left,
                    monitorInfo.rcMonitor.top,
                    monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                    monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                    SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            }
            else
            {
                SetWindowLongPtrW(m_handle, GWL_STYLE, m_windowedStyle);
                SetWindowLongPtrW(m_handle, GWL_EXSTYLE, m_windowedExStyle);
                SetWindowPos(
                    m_handle,
                    nullptr,
                    m_windowedRect.left,
                    m_windowedRect.top,
                    m_windowedRect.right - m_windowedRect.left,
                    m_windowedRect.bottom - m_windowedRect.top,
                    SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER);
            }

            m_fullscreen = enabled;
            UpdateClientSize();
            detail::EmitActiveEvent<WindowFullscreenChangedEvent>(m_fullscreen);

            VC_LOG_INFO(std::string("Fullscreen ") + (m_fullscreen ? "enabled" : "disabled"));
        }

        [[nodiscard]] std::uint32_t GetWidth() const noexcept
        {
            return m_width;
        }

        [[nodiscard]] std::uint32_t GetHeight() const noexcept
        {
            return m_height;
        }

        [[nodiscard]] bool IsFullscreen() const noexcept
        {
            return m_fullscreen;
        }

        [[nodiscard]] bool IsMinimized() const noexcept
        {
            return m_minimized;
        }

        [[nodiscard]] bool ConsumeResize() noexcept
        {
            const bool hadResize = m_resizePending;
            m_resizePending = false;
            return hadResize;
        }

        [[nodiscard]] void* GetNativeHandle() const noexcept
        {
            return m_handle;
        }

    private:
        static LRESULT CALLBACK StaticWindowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_NCCREATE)
            {
                const auto* createStructure = reinterpret_cast<CREATESTRUCTW*>(lParam);
                auto* self = static_cast<Impl*>(createStructure->lpCreateParams);
                SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
                self->m_handle = handle;
            }

            auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
            if (self != nullptr)
            {
                return self->WindowProc(message, wParam, lParam);
            }

            return DefWindowProcW(handle, message, wParam, lParam);
        }

        [[nodiscard]] LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
        {
            detail::HandleInputWindowMessage(message, wParam, lParam);

            switch (message)
            {
            case WM_CLOSE:
                Close();
                return 0;

            case WM_DESTROY:
                m_shouldClose = true;
                m_handle = nullptr;
                PostQuitMessage(0);
                VC_LOG_INFO("Win32 window destroyed.");
                return 0;

            case WM_SIZE:
                m_minimized = wParam == SIZE_MINIMIZED;
                if (!m_minimized)
                {
                    m_width = std::max(1u, static_cast<std::uint32_t>(LOWORD(lParam)));
                    m_height = std::max(1u, static_cast<std::uint32_t>(HIWORD(lParam)));
                    m_resizePending = true;
                    detail::EmitActiveEvent<WindowResizeEvent>(m_width, m_height);
                }
                return 0;

            case WM_SYSKEYDOWN:
                if (wParam == VK_RETURN && (GetKeyState(VK_MENU) & 0x8000) != 0)
                {
                    ToggleFullscreen();
                    return 0;
                }
                break;

            case WM_KEYDOWN:
                if (wParam == VK_F11)
                {
                    ToggleFullscreen();
                    return 0;
                }
                break;
            }

            return DefWindowProcW(m_handle, message, wParam, lParam);
        }

        void CreateNativeWindow()
        {
            WNDCLASSEXW windowClassDescription {};
            windowClassDescription.cbSize = sizeof(windowClassDescription);
            windowClassDescription.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
            windowClassDescription.lpfnWndProc = &Impl::StaticWindowProc;
            windowClassDescription.hInstance = m_instance;
            windowClassDescription.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            windowClassDescription.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            windowClassDescription.lpszClassName = kWindowClassName;

            const ATOM windowClass = RegisterClassExW(&windowClassDescription);
            if (windowClass == 0)
            {
                const DWORD errorCode = GetLastError();
                VC_ASSERT(errorCode == ERROR_CLASS_ALREADY_EXISTS, "Failed to register Win32 window class.");
            }

            const DWORD style = m_specification.resizable
                ? WS_OVERLAPPEDWINDOW
                : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);

            const DWORD exStyle = WS_EX_APPWINDOW;

            RECT windowRect {
                0,
                0,
                static_cast<LONG>(m_specification.width),
                static_cast<LONG>(m_specification.height)
            };

            AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

            m_handle = CreateWindowExW(
                exStyle,
                kWindowClassName,
                m_wideTitle.c_str(),
                style,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                windowRect.right - windowRect.left,
                windowRect.bottom - windowRect.top,
                nullptr,
                nullptr,
                m_instance,
                this);

            VC_ASSERT(m_handle != nullptr, "Failed to create Win32 window.");

            m_windowedStyle = style;
            m_windowedExStyle = exStyle;
            GetWindowRect(m_handle, &m_windowedRect);
            UpdateClientSize();
        }

        void ShowNativeWindow()
        {
            ShowWindow(m_handle, m_specification.startMaximized ? SW_MAXIMIZE : SW_SHOW);
            UpdateWindow(m_handle);

            if (m_specification.startFullscreen)
            {
                SetFullscreen(true);
            }
        }

        void UpdateClientSize()
        {
            RECT clientRect {};
            GetClientRect(m_handle, &clientRect);

            m_width = std::max(1u, static_cast<std::uint32_t>(clientRect.right - clientRect.left));
            m_height = std::max(1u, static_cast<std::uint32_t>(clientRect.bottom - clientRect.top));
        }

    private:
        WindowSpecification m_specification;
        HINSTANCE m_instance = nullptr;
        HWND m_handle = nullptr;
        std::wstring m_wideTitle;
        RECT m_windowedRect {};
        DWORD m_windowedStyle = 0;
        DWORD m_windowedExStyle = 0;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
        bool m_fullscreen = false;
        bool m_minimized = false;
        bool m_shouldClose = false;
        bool m_resizePending = false;
    };

    Window::Window(WindowSpecification specification)
        : m_impl(CreateScope<Impl>(std::move(specification)))
    {
    }

    Window::~Window() = default;

    Window::Window(Window&& other) noexcept = default;

    Window& Window::operator=(Window&& other) noexcept = default;

    bool Window::ProcessMessages()
    {
        return m_impl->ProcessMessages();
    }

    void Window::Close()
    {
        m_impl->Close();
    }

    void Window::SetTitle(const std::string& title)
    {
        m_impl->SetTitle(title);
    }

    void Window::ToggleFullscreen()
    {
        m_impl->ToggleFullscreen();
    }

    void Window::SetFullscreen(bool enabled)
    {
        m_impl->SetFullscreen(enabled);
    }

    std::uint32_t Window::GetWidth() const noexcept
    {
        return m_impl->GetWidth();
    }

    std::uint32_t Window::GetHeight() const noexcept
    {
        return m_impl->GetHeight();
    }

    bool Window::IsFullscreen() const noexcept
    {
        return m_impl->IsFullscreen();
    }

    bool Window::IsMinimized() const noexcept
    {
        return m_impl->IsMinimized();
    }

    bool Window::ConsumeResize() noexcept
    {
        return m_impl->ConsumeResize();
    }

    void* Window::GetNativeHandle() const noexcept
    {
        return m_impl->GetNativeHandle();
    }
}
