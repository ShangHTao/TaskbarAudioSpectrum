#pragma once

#include "platform.h"

namespace tas {
std::wstring MakeOverlayWindowClassName(unsigned serial);
DWORD WINAPI OverlayThreadProc(void*);
}  // namespace tas
