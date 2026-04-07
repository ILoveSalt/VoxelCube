#pragma once

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace vc
{
    template <typename T>
    using Ref = std::shared_ptr<T>;

    template <typename T, typename... Args>
    [[nodiscard]] constexpr Ref<T> CreateRef(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    using Scope = std::unique_ptr<T>;

    template <typename T, typename... Args>
    [[nodiscard]] constexpr Scope<T> CreateScope(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    using Path = std::filesystem::path;
}

#if defined(_MSC_VER)
#define VC_DEBUGBREAK() __debugbreak()
#else
#define VC_DEBUGBREAK() std::abort()
#endif

#if defined(NDEBUG)
#define VC_ASSERT(condition, message) ((void)sizeof(condition))
#else
#define VC_ASSERT(condition, message)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::cerr << "VC_ASSERT failed: " << (message) << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            VC_DEBUGBREAK();                                                                                           \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
#endif
