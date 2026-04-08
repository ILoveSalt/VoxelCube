#pragma once

#include <cmath>

namespace vc
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        constexpr Vec3() = default;

        explicit constexpr Vec3(float scalar) noexcept
            : x(scalar)
            , y(scalar)
            , z(scalar)
        {
        }

        constexpr Vec3(float xValue, float yValue, float zValue) noexcept
            : x(xValue)
            , y(yValue)
            , z(zValue)
        {
        }

        [[nodiscard]] static constexpr Vec3 Zero() noexcept
        {
            return Vec3(0.0f);
        }

        [[nodiscard]] static constexpr Vec3 One() noexcept
        {
            return Vec3(1.0f);
        }

        [[nodiscard]] static constexpr Vec3 Right() noexcept
        {
            return Vec3(1.0f, 0.0f, 0.0f);
        }

        [[nodiscard]] static constexpr Vec3 Up() noexcept
        {
            return Vec3(0.0f, 1.0f, 0.0f);
        }

        [[nodiscard]] static constexpr Vec3 Forward() noexcept
        {
            return Vec3(0.0f, 0.0f, 1.0f);
        }

        [[nodiscard]] constexpr Vec3 operator+() const noexcept
        {
            return *this;
        }

        [[nodiscard]] constexpr Vec3 operator-() const noexcept
        {
            return Vec3(-x, -y, -z);
        }

        [[nodiscard]] constexpr Vec3 operator+(const Vec3& other) const noexcept
        {
            return Vec3(x + other.x, y + other.y, z + other.z);
        }

        [[nodiscard]] constexpr Vec3 operator-(const Vec3& other) const noexcept
        {
            return Vec3(x - other.x, y - other.y, z - other.z);
        }

        [[nodiscard]] constexpr Vec3 operator*(float scalar) const noexcept
        {
            return Vec3(x * scalar, y * scalar, z * scalar);
        }

        [[nodiscard]] constexpr Vec3 operator/(float scalar) const noexcept
        {
            return Vec3(x / scalar, y / scalar, z / scalar);
        }

        constexpr Vec3& operator+=(const Vec3& other) noexcept
        {
            x += other.x;
            y += other.y;
            z += other.z;
            return *this;
        }

        constexpr Vec3& operator-=(const Vec3& other) noexcept
        {
            x -= other.x;
            y -= other.y;
            z -= other.z;
            return *this;
        }

        constexpr Vec3& operator*=(float scalar) noexcept
        {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            return *this;
        }

        constexpr Vec3& operator/=(float scalar) noexcept
        {
            x /= scalar;
            y /= scalar;
            z /= scalar;
            return *this;
        }

        [[nodiscard]] constexpr float LengthSquared() const noexcept
        {
            return x * x + y * y + z * z;
        }

        [[nodiscard]] float Length() const noexcept
        {
            return std::sqrt(LengthSquared());
        }

        [[nodiscard]] Vec3 Normalized(float epsilon = 1.0e-6f) const noexcept
        {
            const float length = Length();
            return length <= epsilon ? Vec3::Zero() : (*this / length);
        }

        [[nodiscard]] bool IsNearlyZero(float epsilon = 1.0e-6f) const noexcept
        {
            return std::fabs(x) <= epsilon && std::fabs(y) <= epsilon && std::fabs(z) <= epsilon;
        }

        [[nodiscard]] bool NearlyEquals(const Vec3& other, float epsilon = 1.0e-6f) const noexcept
        {
            return std::fabs(x - other.x) <= epsilon
                && std::fabs(y - other.y) <= epsilon
                && std::fabs(z - other.z) <= epsilon;
        }

        [[nodiscard]] static constexpr float Dot(const Vec3& left, const Vec3& right) noexcept
        {
            return left.x * right.x + left.y * right.y + left.z * right.z;
        }

        [[nodiscard]] static constexpr Vec3 Cross(const Vec3& left, const Vec3& right) noexcept
        {
            return Vec3(
                left.y * right.z - left.z * right.y,
                left.z * right.x - left.x * right.z,
                left.x * right.y - left.y * right.x);
        }
    };

    [[nodiscard]] inline constexpr Vec3 operator*(float scalar, const Vec3& vector) noexcept
    {
        return vector * scalar;
    }
}
