#pragma once

namespace vc
{
    class Timestep
    {
    public:
        constexpr Timestep() = default;
        explicit constexpr Timestep(double seconds)
            : m_seconds(seconds)
        {
        }

        [[nodiscard]] constexpr double GetSeconds() const
        {
            return m_seconds;
        }

        [[nodiscard]] constexpr double GetMilliseconds() const
        {
            return m_seconds * 1000.0;
        }

        constexpr operator double() const
        {
            return m_seconds;
        }

    private:
        double m_seconds = 0.0;
    };
}
