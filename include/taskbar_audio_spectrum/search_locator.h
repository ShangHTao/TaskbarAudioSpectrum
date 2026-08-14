#pragma once

#include "settings.h"

namespace tas {
enum class SearchHostKind { Unknown, Windows10, Windows11 };
struct SearchLayout {
    HWND taskbar = nullptr;
    DWORD explorerProcessId = 0;
    RECT rect{};
    UINT dpi = 96;
    uint64_t generation = 0;
    bool valid = false;
};
int ScaleForDpi(int value, UINT dpi);
RECT CalculateSpectrumBounds(const SearchLayout& layout,
                             const Settings& settings);
bool HasUsableSearchBoxGeometry(const SearchLayout& layout,
                                const Settings& settings);
bool RectEquals(const RECT& first, const RECT& second);
bool IsSearchExecutableName(PCWSTR executablePath);
SearchHostKind DetectSearchHostKind(PCWSTR executablePath);
bool IsSearchProcessWindow(HWND window);
bool IsSearchInterfaceOpen();
bool IsFullscreenForeground(const SearchLayout& layout);
bool ShouldShowOverlay(const SearchLayout& layout, bool searchInterfaceOpen,
                       bool fullscreenForeground);
DWORD WINAPI SearchLocatorThreadProc(void*);
}  // namespace tas
