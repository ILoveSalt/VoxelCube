#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>

namespace vc
{
    enum class CameraProjectionType : std::uint8_t
    {
        Perspective = 0,
        Orthographic = 1
    };

    [[nodiscard]] inline constexpr std::string_view ToString(CameraProjectionType projectionType) noexcept
    {
        switch (projectionType)
        {
            case CameraProjectionType::Perspective:
                return "Perspective";
            case CameraProjectionType::Orthographic:
                return "Orthographic";
        }

        return "Unknown";
    }

    struct CameraComponent
    {
        CameraProjectionType projectionType = CameraProjectionType::Perspective;
        bool primary = false;
        bool active = true;
        float aspectRatio = 16.0f / 9.0f;
        float verticalFovDegrees = 60.0f;
        float orthographicHeight = 10.0f;
        float nearClip = 0.1f;
        float farClip = 1000.0f;

        [[nodiscard]] static CameraComponent Perspective(
            float verticalFovDegreesValue = 60.0f,
            float aspectRatioValue = 16.0f / 9.0f,
            float nearClipValue = 0.1f,
            float farClipValue = 1000.0f,
            bool primaryCamera = false) noexcept
        {
            CameraComponent camera;
            camera.primary = primaryCamera;
            camera.SetPerspective(verticalFovDegreesValue, aspectRatioValue, nearClipValue, farClipValue);
            return camera;
        }

        [[nodiscard]] static CameraComponent Orthographic(
            float orthographicHeightValue = 10.0f,
            float aspectRatioValue = 16.0f / 9.0f,
            float nearClipValue = -100.0f,
            float farClipValue = 100.0f,
            bool primaryCamera = false) noexcept
        {
            CameraComponent camera;
            camera.primary = primaryCamera;
            camera.SetOrthographic(orthographicHeightValue, aspectRatioValue, nearClipValue, farClipValue);
            return camera;
        }

        void SetPerspective(
            float verticalFovDegreesValue,
            float aspectRatioValue,
            float nearClipValue,
            float farClipValue) noexcept
        {
            projectionType = CameraProjectionType::Perspective;
            verticalFovDegrees = std::clamp(verticalFovDegreesValue, 1.0f, 179.0f);
            aspectRatio = std::max(aspectRatioValue, 1.0e-4f);
            nearClip = std::max(nearClipValue, 1.0e-4f);
            farClip = std::max(farClipValue, nearClip + 1.0e-4f);
        }

        void SetOrthographic(
            float orthographicHeightValue,
            float aspectRatioValue,
            float nearClipValue,
            float farClipValue) noexcept
        {
            projectionType = CameraProjectionType::Orthographic;
            orthographicHeight = std::max(orthographicHeightValue, 1.0e-4f);
            aspectRatio = std::max(aspectRatioValue, 1.0e-4f);
            nearClip = nearClipValue;
            farClip = std::max(farClipValue, nearClip + 1.0e-4f);
        }

        void SetAspectRatio(float aspectRatioValue) noexcept
        {
            aspectRatio = std::max(aspectRatioValue, 1.0e-4f);
        }

        void SetAspectRatioFromViewport(std::uint32_t width, std::uint32_t height) noexcept
        {
            if (height == 0)
            {
                return;
            }

            SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
        }

        [[nodiscard]] bool IsPerspective() const noexcept
        {
            return projectionType == CameraProjectionType::Perspective;
        }

        [[nodiscard]] bool IsOrthographic() const noexcept
        {
            return projectionType == CameraProjectionType::Orthographic;
        }

        [[nodiscard]] bool HasValidClipping(float epsilon = 1.0e-4f) const noexcept
        {
            return farClip > nearClip + epsilon;
        }

        [[nodiscard]] bool HasValidProjection(float epsilon = 1.0e-4f) const noexcept
        {
            if (!HasValidClipping(epsilon) || aspectRatio <= epsilon)
            {
                return false;
            }

            if (IsPerspective())
            {
                return verticalFovDegrees > epsilon && verticalFovDegrees < 180.0f - epsilon;
            }

            return orthographicHeight > epsilon;
        }

        [[nodiscard]] float GetVerticalFovRadians() const noexcept
        {
            return verticalFovDegrees * (std::numbers::pi_v<float> / 180.0f);
        }

        [[nodiscard]] float GetHorizontalFovRadians() const noexcept
        {
            if (!IsPerspective())
            {
                return 0.0f;
            }

            const float tangent = std::tan(GetVerticalFovRadians() * 0.5f);
            return 2.0f * std::atan(tangent * aspectRatio);
        }

        [[nodiscard]] float GetHorizontalFovDegrees() const noexcept
        {
            return GetHorizontalFovRadians() * (180.0f / std::numbers::pi_v<float>);
        }

        [[nodiscard]] float GetOrthographicWidth() const noexcept
        {
            return orthographicHeight * aspectRatio;
        }

        [[nodiscard]] float GetNearPlaneHeight() const noexcept
        {
            if (IsPerspective())
            {
                return 2.0f * std::tan(GetVerticalFovRadians() * 0.5f) * nearClip;
            }

            return orthographicHeight;
        }

        [[nodiscard]] float GetNearPlaneWidth() const noexcept
        {
            return GetNearPlaneHeight() * aspectRatio;
        }
    };
}
