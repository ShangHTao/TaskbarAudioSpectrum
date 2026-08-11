#include <windows.h>

#include <iostream>

#include <taskbar_audio_spectrum/probe_protocol.h>

namespace {

struct ProbeState {
    HWND taskbar = nullptr;
    DWORD explorerProcessId = 0;
    int overlayCount = 0;
    int failures = 0;
    int requiredVisibility = -1;
    bool requireAboveTaskbar = false;
    bool requirePainted = false;
    UINT probeMessage = 0;
};

bool IsWindowAbove(HWND first, HWND second) {
    if (!first || !second) return false;
    for (HWND window = GetWindow(second, GW_HWNDPREV); window;
         window = GetWindow(window, GW_HWNDPREV)) {
        if (window == first) return true;
    }
    return false;
}

bool IsOverlayWindow(HWND window) {
    wchar_t className[256]{};
    if (!GetClassNameW(window, className, ARRAYSIZE(className))) return false;
    const size_t baseLength = wcslen(tas::kOverlayWindowClass);
    return wcsncmp(className, tas::kOverlayWindowClass, baseLength) == 0 &&
           (className[baseLength] == L'\0' ||
            className[baseLength] == L'_');
}

BOOL CALLBACK FindOverlayProc(HWND window, LPARAM parameter) {
    if (!IsOverlayWindow(window)) return TRUE;
    *reinterpret_cast<HWND*>(parameter) = window;
    return FALSE;
}

BOOL CALLBACK EnumProc(HWND window, LPARAM parameter) {
    auto* state = reinterpret_cast<ProbeState*>(parameter);
    if (!IsOverlayWindow(window)) return TRUE;

    ++state->overlayCount;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    RECT rect{};
    GetWindowRect(window, &rect);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const HWND parentOrOwner = GetParent(window);
    const HWND owner = GetWindow(window, GW_OWNER);
    const bool aboveTaskbar = IsWindowAbove(window, state->taskbar);
    DWORD_PTR probeFlags = 0;
    const bool probeResponded = state->probeMessage && SendMessageTimeoutW(
        window, state->probeMessage, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000,
        &probeFlags);
    const bool painted = probeResponded &&
        (probeFlags & tas::kOverlayPainted) != 0;
    const bool validOwner = owner == state->taskbar &&
                            parentOrOwner == state->taskbar;
    const bool valid = validOwner && !(style & WS_CHILD) &&
        (style & WS_POPUP) && (extendedStyle & WS_EX_LAYERED) &&
        (extendedStyle & WS_EX_TRANSPARENT) &&
        (extendedStyle & WS_EX_TOOLWINDOW) &&
        (extendedStyle & WS_EX_NOACTIVATE) &&
        processId != state->explorerProcessId;
    std::wcout << L"overlay=" << window << L" pid=" << processId
               << L" visible=" << IsWindowVisible(window)
               << L" rect=" << rect.left << L"," << rect.top << L","
               << rect.right << L"," << rect.bottom
               << L" owner=" << owner << L" style=0x" << std::hex
               << style << L" exstyle=0x" << extendedStyle << std::dec
               << L" aboveTaskbar=" << aboveTaskbar
               << L" probe=0x" << std::hex << probeFlags << std::dec
               << L" painted=" << painted << L" valid=" << valid << L"\n";
    if (!valid) ++state->failures;
    if (state->requiredVisibility >= 0 &&
        ((IsWindowVisible(window) != FALSE) !=
         (state->requiredVisibility == 1))) {
        ++state->failures;
    }
    if (state->requireAboveTaskbar && !aboveTaskbar) ++state->failures;
    if (state->requirePainted && !painted) ++state->failures;
    return TRUE;
}

}  // namespace

int wmain(int argumentCount, wchar_t** arguments) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool requireOverlay = false;
    bool reclaimTaskbar = false;
    ProbeState state;
    for (int index = 1; index < argumentCount; ++index) {
        if (wcscmp(arguments[index], L"--require-overlay") == 0) {
            requireOverlay = true;
        } else if (wcscmp(arguments[index], L"--require-visible") == 0) {
            state.requiredVisibility = 1;
        } else if (wcscmp(arguments[index], L"--require-hidden") == 0) {
            state.requiredVisibility = 0;
        } else if (wcscmp(arguments[index],
                          L"--require-above-taskbar") == 0) {
            state.requireAboveTaskbar = true;
        } else if (wcscmp(arguments[index], L"--require-painted") == 0) {
            state.requirePainted = true;
        } else if (wcscmp(arguments[index], L"--reclaim-taskbar") == 0) {
            reclaimTaskbar = true;
        }
    }
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!taskbar) {
        std::wcerr << L"Taskbar window was not found.\n";
        return 2;
    }
    state.taskbar = taskbar;
    state.probeMessage = RegisterWindowMessageW(tas::kOverlayProbeMessageName);
    if (!state.probeMessage) {
        std::wcerr << L"Overlay probe message registration failed.\n";
        return 2;
    }

    if (reclaimTaskbar) {
        HWND overlay = nullptr;
        EnumWindows(FindOverlayProc, reinterpret_cast<LPARAM>(&overlay));
        if (!overlay) {
            std::wcerr << L"Overlay window was not found for z-order test.\n";
            return 3;
        }
        if (!SetWindowPos(taskbar, HWND_TOPMOST, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                              SWP_NOOWNERZORDER)) {
            std::wcerr << L"Failed to move taskbar above overlay: "
                       << GetLastError() << L".\n";
            return 4;
        }
        std::wcout << L"Taskbar z-order reclaim requested.\n";
        return 0;
    }

    GetWindowThreadProcessId(taskbar, &state.explorerProcessId);
    DWORD_PTR ignored = 0;
    const LRESULT responsive = SendMessageTimeoutW(
        taskbar, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &ignored);
    std::wcout << L"taskbar=" << taskbar
               << L" explorerPid=" << state.explorerProcessId
               << L" responsive=" << (responsive != 0) << L"\n";
    if (!responsive) ++state.failures;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&state));
    if (requireOverlay && state.overlayCount != 1) {
        std::wcerr << L"Expected exactly one overlay; found "
                   << state.overlayCount << L".\n";
        ++state.failures;
    }
    return state.failures ? 1 : 0;
}
