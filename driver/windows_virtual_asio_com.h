#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace sar::driver {

// Stable product identity. Installer and both architecture builds must use the
// same CLSID in their matching registry views.
inline constexpr CLSID kWindowsVirtualAsioClsid = {
    0x7f16c8a9,
    0x4a0c,
    0x4d31,
    {0x9a, 0x5b, 0x2c, 0x6e, 0x7f, 0x8d, 0x10, 0x42},
};

inline constexpr wchar_t kWindowsVirtualAsioClsidString[] =
    L"{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}";
inline constexpr wchar_t kWindowsVirtualAsioDisplayName[] =
    L"System Audio Route Virtual ASIO";

}  // namespace sar::driver
