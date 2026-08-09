#include <taskbar_audio_spectrum/overlay_window.h>
#include <taskbar_audio_spectrum/host.h>
#include <taskbar_audio_spectrum/probe_protocol.h>
#include <taskbar_audio_spectrum/search_locator.h>
#include <taskbar_audio_spectrum/spectrum_analysis.h>

#include "runtime_context.h"
#include "registry_change_watcher.h"

namespace tas {

namespace {

constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT_PTR kStateSafetyTimer = 2;
constexpr UINT kShellStateChangedMessage = WM_APP + 1;
constexpr UINT kStateSafetyRefreshMs = 30000;

struct WindowState {
    ApplicationContext* context = nullptr;
    SearchLayout layout{};
    std::array<float, kMaxBars> displayed{};
    std::array<PeakState, kMaxBars> peaks{};
    HDC memoryDc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ originalBitmap = nullptr;
    void* pixels = nullptr;
    std::vector<uint32_t> framePixels;
    std::vector<uint32_t> presentedPixels;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    // TAS_WINDHAWK_EXCLUDE_BEGIN
    size_t paintedPixels = 0;
    UINT probeMessage = 0;
    // TAS_WINDHAWK_EXCLUDE_END
    ULONGLONG lastFrameTick = 0;
    ULONGLONG lastAudibleTick = 0;
    HWND ownerTaskbar = nullptr;
    DWORD searchMode = 0;
    bool searchInterfaceOpen = false;
    bool searchInterfaceWasOpen = false;
    ULONGLONG lastZOrderLog = 0;
    bool positioned = false;
    bool eligible = false;
    bool visible = false;
    bool updateErrorLogged = false;
    bool zOrderErrorLogged = false;
};

thread_local HWND g_shellStateTargetWindow = nullptr;

void CALLBACK ShellStateWinEventProc(HWINEVENTHOOK, DWORD event,
                                     HWND eventWindow, LONG objectId,
                                     LONG childId, DWORD, DWORD) {
    if (!g_shellStateTargetWindow) return;
    if (event == EVENT_OBJECT_LOCATIONCHANGE) {
        if (!eventWindow || objectId != OBJID_WINDOW ||
            childId != CHILDID_SELF) {
            return;
        }
        const HWND foreground = GetForegroundWindow();
        if (!foreground ||
            GetAncestor(eventWindow, GA_ROOT) !=
                GetAncestor(foreground, GA_ROOT)) {
            return;
        }
    }
    PostMessageW(g_shellStateTargetWindow, kShellStateChangedMessage,
                 event, 0);
}

bool IsWindowAbove(HWND first, HWND second) {
    if (!first || !second) return false;
    for (HWND window = GetWindow(second, GW_HWNDPREV); window;
         window = GetWindow(window, GW_HWNDPREV)) {
        if (window == first) return true;
    }
    return false;
}

bool EnsureAboveTaskbar(HWND window, WindowState* state) {
    if (!state || !state->visible || !state->layout.taskbar ||
        !IsWindow(state->layout.taskbar)) {
        return true;
    }
    if (!IsWindowAbove(state->layout.taskbar, window)) return true;
    if (!SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                          SWP_NOOWNERZORDER)) {
        if (!state->zOrderErrorLogged) {
            Log(L"Failed to restore overlay above the taskbar: %u",
                GetLastError());
            state->zOrderErrorLogged = true;
        }
        return false;
    }

    state->zOrderErrorLogged = false;
    const ULONGLONG now = GetTickCount64();
    if (!state->lastZOrderLog || now - state->lastZOrderLog >= 60000) {
        Log(L"Overlay z-order restored above the taskbar");
        state->lastZOrderLog = now;
    }
    return true;
}

bool EnsureTaskbarOwner(HWND window, WindowState* state,
                        const SearchLayout& layout) {
    if (!window || !state || !layout.taskbar ||
        !IsWindow(layout.taskbar)) {
        return false;
    }
    if (state->ownerTaskbar == layout.taskbar &&
        reinterpret_cast<HWND>(GetWindowLongPtrW(window, GWLP_HWNDPARENT)) ==
            layout.taskbar) {
        return true;
    }

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(
        window, GWLP_HWNDPARENT,
        reinterpret_cast<LONG_PTR>(layout.taskbar));
    if (!previous && GetLastError() != ERROR_SUCCESS) {
        Log(L"Failed to assign taskbar as overlay owner: %u", GetLastError());
        return false;
    }
    state->ownerTaskbar = layout.taskbar;
    if (!SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                          SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
        Log(L"Failed to apply taskbar overlay ownership: %u", GetLastError());
        return false;
    }
    Log(L"Overlay attached as an owned top-level window: explorer=%lu",
        layout.explorerProcessId);
    return true;
}

void HideOverlay(HWND window, WindowState* state, bool deactivate = true) {
    if (!state) return;
    if (deactivate) SetVisualizerActive(*state->context, false);
    state->peaks = {};
    if (deactivate) state->displayed = {};
    state->lastFrameTick = 0;
    if (!state->visible) return;
    ShowWindow(window, SW_HIDE);
    SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                     SWP_NOOWNERZORDER);
    state->visible = false;
    // TAS_WINDHAWK_EXCLUDE_BEGIN
    state->paintedPixels = 0;
    // TAS_WINDHAWK_EXCLUDE_END
}

bool ApplyWindowLayout(HWND window, WindowState* state,
                       const SearchLayout& layout) {
    if (!state || !layout.valid) return false;
    const int width = layout.rect.right - layout.rect.left;
    const int height = layout.rect.bottom - layout.rect.top;
    if (width <= 0 || height <= 0) return false;
    if (!EnsureTaskbarOwner(window, state, layout)) return false;
    const bool changed = state->layout.generation != layout.generation ||
                         !RectEquals(state->layout.rect, layout.rect);
    state->layout = layout;
    if (!changed && state->positioned) return true;
    const BOOL positioned = SetWindowPos(
        window, HWND_TOPMOST, layout.rect.left, layout.rect.top, width, height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER |
            (state->visible ? SWP_SHOWWINDOW : 0));
    state->positioned = positioned != FALSE;
    return state->positioned;
}

void DestroySurface(WindowState* state) {
    if (!state) return;
    if (state->memoryDc && state->originalBitmap) {
        SelectObject(state->memoryDc, state->originalBitmap);
    }
    if (state->bitmap) DeleteObject(state->bitmap);
    if (state->memoryDc) DeleteDC(state->memoryDc);
    state->memoryDc = nullptr;
    state->bitmap = nullptr;
    state->originalBitmap = nullptr;
    state->pixels = nullptr;
    state->surfaceWidth = 0;
    state->surfaceHeight = 0;
    state->framePixels.clear();
    state->presentedPixels.clear();
    // TAS_WINDHAWK_EXCLUDE_BEGIN
    state->paintedPixels = 0;
    // TAS_WINDHAWK_EXCLUDE_END
}

bool EnsureSurface(WindowState* state, int width, int height) {
    if (state->memoryDc && state->bitmap && state->pixels &&
        state->surfaceWidth == width && state->surfaceHeight == height) {
        return true;
    }
    if (!state->memoryDc) {
        state->memoryDc = CreateCompatibleDC(nullptr);
        if (!state->memoryDc) return false;
    }
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        nullptr, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        return false;
    }
    HGDIOBJ previous = SelectObject(state->memoryDc, bitmap);
    if (!previous || previous == HGDI_ERROR) {
        DeleteObject(bitmap);
        return false;
    }
    if (state->bitmap) {
        DeleteObject(state->bitmap);
    } else {
        state->originalBitmap = previous;
    }
    state->bitmap = bitmap;
    state->pixels = pixels;
    state->surfaceWidth = width;
    state->surfaceHeight = height;
    state->presentedPixels.clear();
    return true;
}

void DrawSpectrum(HWND window, WindowState* state) {
    ApplicationContext& context = *state->context;
    const Settings& settings = context.settings;
    const int width = state->layout.rect.right - state->layout.rect.left;
    const int height = state->layout.rect.bottom - state->layout.rect.top;
    if (width <= 0 || height <= 0) return;

    const int bars = settings.barCount;
    const ULONGLONG frameTick = GetTickCount64();
    const float frameSeconds = state->lastFrameTick
        ? std::clamp((frameTick - state->lastFrameTick) / 1000.0f,
                     0.0f, 0.25f)
        : 1.0f / settings.fps;
    state->lastFrameTick = frameTick;
    bool audible = false;
    std::array<float, kMaxBars> shaped{};
    for (int index = 0; index < bars; ++index) {
        const float target =
            context.bands[index].load(std::memory_order_relaxed);
        state->displayed[index] = SmoothDisplayLevel(
            state->displayed[index], target, frameSeconds,
            settings.attackMs, settings.releaseMs);
        if (state->displayed[index] < 0.0005f) state->displayed[index] = 0.0f;
        if (state->displayed[index] > settings.silenceThreshold) {
            audible = true;
        }
        shaped[index] = std::pow(state->displayed[index], 0.72f);
        if (settings.peakEnabled) {
            UpdatePeak(&state->peaks[index], shaped[index], frameSeconds,
                       settings.peakHoldMs / 1000.0f,
                       settings.peakGravity);
        } else {
            state->peaks[index] = {};
        }
    }
    if (audible || !state->lastAudibleTick) {
        state->lastAudibleTick = frameTick;
    }
    const bool silenceDelayElapsed = !audible &&
        frameTick - state->lastAudibleTick >=
            static_cast<ULONGLONG>(settings.silenceHideDelayMs);
    if (settings.opacity == 0 ||
        (settings.hideWhenSilent && silenceDelayElapsed)) {
        HideOverlay(window, state, false);
        return;
    }

    const RECT spectrumBounds =
        CalculateSpectrumBounds(state->layout, settings);
    const int spectrumLeft = static_cast<int>(spectrumBounds.left);
    const int spectrumRight = static_cast<int>(spectrumBounds.right);
    const int spectrumTop = static_cast<int>(spectrumBounds.top);
    const int spectrumBottom = static_cast<int>(spectrumBounds.bottom);
    const int usableWidth = spectrumRight - spectrumLeft;
    const int maximumHeight = spectrumBottom - spectrumTop;
    if (usableWidth <= 0 || maximumHeight <= 0) {
        HideOverlay(window, state, false);
        return;
    }
    if (!EnsureSurface(state, width, height)) {
        if (!state->updateErrorLogged) {
            Log(L"Failed to allocate spectrum surface: %u", GetLastError());
            state->updateErrorLogged = true;
        }
        HideOverlay(window, state);
        return;
    }
    const size_t pixelCount = static_cast<size_t>(width) * height;
    state->framePixels.assign(pixelCount, 0);
    auto* buffer = state->framePixels.data();
    const auto makePixel = [](COLORREF color, BYTE alpha) -> uint32_t {
        return (static_cast<uint32_t>(alpha) << 24) |
               (static_cast<uint32_t>(GetRValue(color) * alpha / 255) << 16) |
               (static_cast<uint32_t>(GetGValue(color) * alpha / 255) << 8) |
               (GetBValue(color) * alpha / 255);
    };
    // TAS_WINDHAWK_EXCLUDE_BEGIN
    size_t paintedPixels = 0;
    // TAS_WINDHAWK_EXCLUDE_END
    const auto fillRect = [&](int x0, int y0, int x1, int y1,
                              uint32_t pixel) {
        x0 = std::clamp(x0, 0, width);
        x1 = std::clamp(x1, 0, width);
        y0 = std::clamp(y0, 0, height);
        y1 = std::clamp(y1, 0, height);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                uint32_t& destination = buffer[y * width + x];
                // TAS_WINDHAWK_EXCLUDE_BEGIN
                if (!destination && (pixel >> 24)) ++paintedPixels;
                // TAS_WINDHAWK_EXCLUDE_END
                destination = pixel;
            }
        }
    };

    const float slot = usableWidth / static_cast<float>(bars);
    const int barWidth = std::max(
        1, static_cast<int>(slot * settings.barWidthPercent / 100.0f));
    const int peakHeight = std::clamp(
        ScaleForDpi(settings.peakHeight, state->layout.dpi),
        1, maximumHeight);
    const int peakGap =
        std::max(0, ScaleForDpi(settings.peakGap, state->layout.dpi));
    for (int index = 0; index < bars; ++index) {
        const LevelPixelCoverage barCoverage =
            CalculateLevelPixelCoverage(shaped[index], maximumHeight);
        const int barHeight = barCoverage.fullPixels;
        const int x0 = spectrumLeft +
                       static_cast<int>(index * slot +
                                        (slot - barWidth) / 2);
        const int x1 = std::min(spectrumRight, x0 + barWidth);
        const int y1 = spectrumBottom;
        const int y0 = std::max(spectrumTop, y1 - barHeight);
        const float mix = index /
            static_cast<float>(std::max(1, bars - 1));
        const BYTE red = static_cast<BYTE>(
            GetRValue(settings.color) * (1 - mix) +
            GetRValue(settings.secondColor) * mix);
        const BYTE green = static_cast<BYTE>(
            GetGValue(settings.color) * (1 - mix) +
            GetGValue(settings.secondColor) * mix);
        const BYTE blue = static_cast<BYTE>(
            GetBValue(settings.color) * (1 - mix) +
            GetBValue(settings.secondColor) * mix);
        const COLORREF barColor = RGB(red, green, blue);
        if (barHeight > 0) {
            fillRect(x0, y0, x1, y1,
                     makePixel(barColor, settings.opacity));
        }
        if (barCoverage.partialPixel > 0.0f && y0 > spectrumTop) {
            const BYTE partialOpacity = static_cast<BYTE>(std::clamp(
                static_cast<int>(std::lround(
                    settings.opacity * barCoverage.partialPixel)),
                0, 255));
            if (partialOpacity > 0) {
                fillRect(x0, y0 - 1, x1, y0,
                         makePixel(barColor, partialOpacity));
            }
        }

        if (settings.peakEnabled) {
            const int peakBarHeight = CalculateRenderedLevelHeight(
                state->peaks[index].level, maximumHeight, 0);
            const int peakBottom = CalculatePeakBlockBottom(
                peakBarHeight, spectrumTop, y1, peakHeight, peakGap,
                settings.peakShowWhenSilent);
            if (peakBottom < 0) continue;
            const int peakTop = peakBottom - peakHeight;
            const BYTE peakRed = static_cast<BYTE>((red * 3 + 255) / 4);
            const BYTE peakGreen = static_cast<BYTE>((green * 3 + 255) / 4);
            const BYTE peakBlue = static_cast<BYTE>((blue * 3 + 255) / 4);
            const BYTE peakOpacity = static_cast<BYTE>(
                std::min(255, static_cast<int>(settings.opacity) + 55));
            fillRect(x0, peakTop, x1, peakBottom,
                     makePixel(RGB(peakRed, peakGreen, peakBlue),
                               peakOpacity));
        }
    }

    SIZE size{width, height};
    POINT source{};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (state->visible && state->presentedPixels == state->framePixels) {
        EnsureAboveTaskbar(window, state);
        return;
    }
    memcpy(state->pixels, state->framePixels.data(),
           pixelCount * sizeof(uint32_t));
    if (!UpdateLayeredWindow(window, nullptr, nullptr, &size, state->memoryDc,
                             &source, 0, &blend, ULW_ALPHA)) {
        if (!state->updateErrorLogged) {
            Log(L"UpdateLayeredWindow failed: %u", GetLastError());
            state->updateErrorLogged = true;
        }
        return;
    }
    state->updateErrorLogged = false;
    state->presentedPixels = state->framePixels;
    // TAS_WINDHAWK_EXCLUDE_BEGIN
    state->paintedPixels = paintedPixels;
    // TAS_WINDHAWK_EXCLUDE_END
    if (!state->visible) {
        SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                         SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        state->visible = true;
        SetVisualizerActive(context, true);
    }
    EnsureAboveTaskbar(window, state);
}

void RefreshOverlayState(HWND window, WindowState* state,
                         bool refreshSearchMode,
                         bool refreshSearchInterface) {
    if (!state) return;
    if (refreshSearchMode) {
        state->searchMode = GetSearchMode();
    }
    if (refreshSearchInterface) {
        state->searchInterfaceOpen = IsSearchInterfaceOpen();
        if (state->searchInterfaceOpen) {
            if (!state->searchInterfaceWasOpen) {
                Log(L"Search interface open detected");
            }
            state->searchInterfaceWasOpen = true;
        } else if (state->searchInterfaceWasOpen) {
            state->searchInterfaceWasOpen = false;
            state->positioned = false;
            Log(L"Search interface dismissed; restoring owned overlay");
        }
    }

    SearchLayout latest;
    if (!ReadLatestSearchLayout(*state->context, &latest)) {
        state->eligible = false;
        HideOverlay(window, state);
        return;
    }
    const bool fullscreenForeground = IsFullscreenForeground(latest);
    if (!ShouldShowOverlay(latest, state->searchMode,
                           state->searchInterfaceOpen,
                           fullscreenForeground)) {
        state->eligible = false;
        HideOverlay(window, state);
        return;
    }
    state->eligible = true;
    SetVisualizerActive(*state->context, true);
    const bool changed =
        state->layout.generation != latest.generation;
    const DWORD previousExplorer = state->layout.explorerProcessId;
    if (!ApplyWindowLayout(window, state, latest)) {
        Log(L"Failed to position overlay: %u", GetLastError());
        state->eligible = false;
        HideOverlay(window, state);
        return;
    }
    EnsureAboveTaskbar(window, state);
    if (changed) {
        const RECT spectrumBounds = CalculateSpectrumBounds(
            latest, state->context->settings);
        Log(previousExplorer != latest.explorerProcessId
                ? L"Search box attached: explorer=%lu left=%ld top=%ld width=%ld height=%ld insets=%d/%d/%d/%d dpi=%u"
                : L"Spectrum layout updated: explorer=%lu left=%ld top=%ld width=%ld height=%ld insets=%d/%d/%d/%d dpi=%u",
            latest.explorerProcessId, latest.rect.left,
            latest.rect.top, latest.rect.right - latest.rect.left,
            latest.rect.bottom - latest.rect.top,
            spectrumBounds.left,
            latest.rect.right - latest.rect.left - spectrumBounds.right,
            spectrumBounds.top,
            latest.rect.bottom - latest.rect.top - spectrumBounds.bottom,
            latest.dpi);
    }
}

LRESULT CALLBACK OverlayWindowProc(HWND window, UINT message,
                                   WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    // TAS_WINDHAWK_EXCLUDE_BEGIN
    if (state && state->probeMessage && message == state->probeMessage) {
        LRESULT flags = 0;
        if (state->positioned) flags |= kOverlayPositioned;
        if (state->eligible) flags |= kOverlayEligible;
        if (state->visible && IsWindowVisible(window)) flags |= kOverlayVisible;
        if (state->paintedPixels) flags |= kOverlayPainted;
        return flags;
    }
    // TAS_WINDHAWK_EXCLUDE_END
    switch (message) {
        case WM_NCCREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(
                window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return TRUE;
        }
        case kShellStateChangedMessage:
            RefreshOverlayState(
                window, state, false,
                wParam != EVENT_OBJECT_LOCATIONCHANGE);
            return 0;
        case WM_TIMER:
            if (!state) return 0;
            if (wParam == kStateSafetyTimer) {
                RefreshOverlayState(window, state, true, true);
            } else if (wParam == kFrameTimer && state->positioned &&
                       state->eligible) {
                DrawSpectrum(window, state);
            }
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_DESTROY:
            if (state) SetVisualizerActive(*state->context, false);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

DWORD WINAPI OverlayThreadProc(void* parameter) {
    auto& context = *static_cast<ApplicationContext*>(parameter);
    EnablePerMonitorDpiAwarenessForThread();
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = OverlayWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kOverlayWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    const bool registeredWindowClass = RegisterClassExW(&windowClass) != 0;
    if (!registeredWindowClass && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Log(L"Failed to register overlay class: %u", GetLastError());
        return 0;
    }

    // TAS_WINDHAWK_EXCLUDE_BEGIN
    const UINT probeMessage = RegisterWindowMessageW(kOverlayProbeMessageName);
    if (!probeMessage) {
        Log(L"Failed to register the overlay probe message: %u",
            GetLastError());
    }
    // TAS_WINDHAWK_EXCLUDE_END
    const UINT frameInterval = std::clamp<UINT>(
        static_cast<UINT>((1000 + context.settings.fps / 2) /
                          context.settings.fps),
        USER_TIMER_MINIMUM, 1000);
    RegistryChangeWatcher searchModeWatcher;
    if (searchModeWatcher.Start(kSearchSettingsRegistryPath)) {
        Log(L"Overlay search mode watcher started");
    } else {
        Log(L"Overlay search mode watcher unavailable; using safety refresh");
    }
    bool rendererStarted = false;
    while (WaitForSingleObject(context.stopEvent.get(), 0) != WAIT_OBJECT_0) {
        WindowState state;
        state.context = &context;
        // TAS_WINDHAWK_EXCLUDE_BEGIN
        state.probeMessage = probeMessage;
        // TAS_WINDHAWK_EXCLUDE_END
        HWND window = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                WS_EX_NOACTIVATE,
            kOverlayWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr,
            nullptr, windowClass.hInstance, &state);
        if (!window) {
            Log(L"Failed to create overlay window: %u", GetLastError());
            break;
        }
        if (!SetTimer(window, kFrameTimer, frameInterval, nullptr) ||
            !SetTimer(window, kStateSafetyTimer,
                      kStateSafetyRefreshMs, nullptr)) {
            Log(L"Failed to create overlay timer: %u", GetLastError());
            DestroyWindow(window);
            DestroySurface(&state);
            break;
        }
        g_shellStateTargetWindow = window;
        constexpr DWORD eventHookFlags =
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        const HWINEVENTHOOK foregroundHook = SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
            ShellStateWinEventProc, 0, 0, eventHookFlags);
        const HWINEVENTHOOK locationHook = SetWinEventHook(
            EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
            nullptr, ShellStateWinEventProc, 0, 0, eventHookFlags);
        if (!foregroundHook || !locationHook) {
            Log(L"Foreground event watcher incomplete: %u", GetLastError());
        }
        Log(rendererStarted
                ? L"Spectrum renderer recreated after taskbar replacement"
                : L"Spectrum renderer started as an owned top-level overlay");
        rendererStarted = true;

        RefreshOverlayState(window, &state, true, true);
        bool messageLoopFailed = false;
        bool ownerReplaced = false;
        while (!ownerReplaced) {
            HANDLE waits[3] = {context.stopEvent.get(),
                               context.layoutChangedEvent.get()};
            DWORD waitCount = 2;
            DWORD searchModeWaitIndex = MAXDWORD;
            if (searchModeWatcher.changedEvent()) {
                searchModeWaitIndex = waitCount;
                waits[waitCount++] = searchModeWatcher.changedEvent();
            }
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(
                waitCount, waits, INFINITE, QS_ALLINPUT,
                MWMO_INPUTAVAILABLE);
            if (waitResult == WAIT_FAILED) {
                Log(L"Overlay message wait failed: %u", GetLastError());
                messageLoopFailed = true;
                break;
            }
            if (waitResult == WAIT_OBJECT_0) break;
            if (waitResult == WAIT_OBJECT_0 + 1) {
                RefreshOverlayState(window, &state, false, false);
                continue;
            }
            if (searchModeWaitIndex != MAXDWORD &&
                waitResult == WAIT_OBJECT_0 + searchModeWaitIndex) {
                if (!searchModeWatcher.Arm()) {
                    Log(L"Overlay search mode watcher could not be rearmed");
                    searchModeWatcher.Stop();
                }
                RefreshOverlayState(window, &state, true, false);
                continue;
            }
            if (waitResult != WAIT_OBJECT_0 + waitCount) {
                Log(L"Overlay message wait returned unexpected status: %u",
                    waitResult);
                messageLoopFailed = true;
                break;
            }

            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    ownerReplaced = true;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        g_shellStateTargetWindow = nullptr;
        if (foregroundHook) UnhookWinEvent(foregroundHook);
        if (locationHook) UnhookWinEvent(locationHook);
        KillTimer(window, kFrameTimer);
        KillTimer(window, kStateSafetyTimer);
        if (IsWindow(window)) DestroyWindow(window);
        DestroySurface(&state);
        if (messageLoopFailed ||
            WaitForSingleObject(context.stopEvent.get(), 0) == WAIT_OBJECT_0) {
            break;
        }
        Log(L"Overlay owner was replaced; recreating renderer");
    }
    if (registeredWindowClass) {
        UnregisterClassW(kOverlayWindowClass, windowClass.hInstance);
    }
    Log(L"Spectrum renderer stopped");
    return 0;
}

}  // namespace tas
