#pragma once

#include <cstdint>
#include <string>

#include <VoxelCube/Core/Base.hpp>

namespace vc
{
    struct WindowSpecification
    {
        std::string title = "VoxelCube";
        std::uint32_t width = 1280;
        std::uint32_t height = 720;
        bool resizable = true;
        bool startMaximized = false;
        bool startFullscreen = false;
    };

    class Window
    {
    public:
        explicit Window(WindowSpecification specification = {});
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) noexcept;
        Window& operator=(Window&&) noexcept;

        [[nodiscard]] bool ProcessMessages();
        void Close();

        void SetTitle(const std::string& title);
        void ToggleFullscreen();
        void SetFullscreen(bool enabled);

        [[nodiscard]] std::uint32_t GetWidth() const noexcept;
        [[nodiscard]] std::uint32_t GetHeight() const noexcept;
        [[nodiscard]] bool IsFullscreen() const noexcept;
        [[nodiscard]] bool IsMinimized() const noexcept;
        [[nodiscard]] bool ConsumeResize() noexcept;
        [[nodiscard]] void* GetNativeHandle() const noexcept;

    private:
        class Impl;
        Scope<Impl> m_impl;
    };
}
