#pragma once

#include <cmath>
#include <numbers>

#include <VoxelCube/Core/Math.hpp>

namespace vc
{
    struct TransformComponent
    {
        Vec3 translation = Vec3::Zero();
        Vec3 rotationEulerDegrees = Vec3::Zero();
        Vec3 scale = Vec3::One();

        [[nodiscard]] static constexpr TransformComponent Identity() noexcept
        {
            return {};
        }

        [[nodiscard]] static constexpr TransformComponent FromTranslation(const Vec3& translationValue) noexcept
        {
            TransformComponent transform;
            transform.translation = translationValue;
            return transform;
        }

        [[nodiscard]] static constexpr TransformComponent FromTRS(
            const Vec3& translationValue,
            const Vec3& rotationEulerDegreesValue = Vec3::Zero(),
            const Vec3& scaleValue = Vec3::One()) noexcept
        {
            TransformComponent transform;
            transform.translation = translationValue;
            transform.rotationEulerDegrees = rotationEulerDegreesValue;
            transform.scale = scaleValue;
            return transform;
        }

        void Reset() noexcept
        {
            *this = Identity();
        }

        void Translate(const Vec3& delta) noexcept
        {
            translation += delta;
        }

        void Rotate(const Vec3& deltaDegrees) noexcept
        {
            rotationEulerDegrees += deltaDegrees;
        }

        void SetUniformScale(float uniformScale) noexcept
        {
            scale = Vec3(uniformScale);
        }

        [[nodiscard]] bool HasUniformScale(float epsilon = 1.0e-4f) const noexcept
        {
            return NearlyEqual(scale.x, scale.y, epsilon) && NearlyEqual(scale.x, scale.z, epsilon);
        }

        [[nodiscard]] bool HasValidScale(float epsilon = 1.0e-6f) const noexcept
        {
            return std::fabs(scale.x) > epsilon && std::fabs(scale.y) > epsilon && std::fabs(scale.z) > epsilon;
        }

        [[nodiscard]] bool IsIdentity(float epsilon = 1.0e-4f) const noexcept
        {
            return translation.IsNearlyZero(epsilon)
                && rotationEulerDegrees.IsNearlyZero(epsilon)
                && scale.NearlyEquals(Vec3::One(), epsilon);
        }

        [[nodiscard]] Vec3 GetRightDirection() const noexcept
        {
            return RotateBasisVector(Vec3::Right()).Normalized();
        }

        [[nodiscard]] Vec3 GetUpDirection() const noexcept
        {
            return RotateBasisVector(Vec3::Up()).Normalized();
        }

        [[nodiscard]] Vec3 GetForwardDirection() const noexcept
        {
            return RotateBasisVector(Vec3::Forward()).Normalized();
        }

    private:
        [[nodiscard]] static bool NearlyEqual(float left, float right, float epsilon) noexcept
        {
            return std::fabs(left - right) <= epsilon;
        }

        [[nodiscard]] static float DegreesToRadians(float degrees) noexcept
        {
            return degrees * (std::numbers::pi_v<float> / 180.0f);
        }

        [[nodiscard]] static Vec3 RotateAroundX(const Vec3& vector, float radians) noexcept
        {
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            return Vec3(
                vector.x,
                vector.y * cosine - vector.z * sine,
                vector.y * sine + vector.z * cosine);
        }

        [[nodiscard]] static Vec3 RotateAroundY(const Vec3& vector, float radians) noexcept
        {
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            return Vec3(
                vector.x * cosine + vector.z * sine,
                vector.y,
                -vector.x * sine + vector.z * cosine);
        }

        [[nodiscard]] static Vec3 RotateAroundZ(const Vec3& vector, float radians) noexcept
        {
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            return Vec3(
                vector.x * cosine - vector.y * sine,
                vector.x * sine + vector.y * cosine,
                vector.z);
        }

        [[nodiscard]] Vec3 RotateBasisVector(const Vec3& basisVector) const noexcept
        {
            // Yaw(Y) -> Pitch(X) -> Roll(Z) keeps the orientation deterministic for gameplay systems.
            Vec3 rotated = basisVector;
            rotated = RotateAroundY(rotated, DegreesToRadians(rotationEulerDegrees.y));
            rotated = RotateAroundX(rotated, DegreesToRadians(rotationEulerDegrees.x));
            rotated = RotateAroundZ(rotated, DegreesToRadians(rotationEulerDegrees.z));
            return rotated;
        }
    };
}
