#pragma once

#include <cstdint>

namespace vc::detail
{
    void HandleInputWindowMessage(std::uintptr_t message, std::uintptr_t wParam, std::intptr_t lParam);
}
