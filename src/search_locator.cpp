#include <taskbar_audio_spectrum/search_locator.h>
#include <taskbar_audio_spectrum/host.h>

#include <uiautomation.h>
#include <dwmapi.h>

#include "runtime_context.h"
#include "registry_change_watcher.h"

#include <memory>
#include <new>

namespace tas {

namespace {

constexpr DWORD kNormalLayoutRefreshMs = 30000;
constexpr DWORD kRapidLayoutRefreshMs = 100;
constexpr ULONGLONG kRapidLayoutRefreshDurationMs = 2500;

struct LocatorTarget {
    ApplicationContext* context = nullptr;
    HWND taskbar = nullptr;
    DWORD explorerProcessId = 0;
};

class LayoutPropertyChangedHandler final
    : public IUIAutomationPropertyChangedEventHandler {
public:
    LayoutPropertyChangedHandler()
        : changedEvent_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

    ~LayoutPropertyChangedHandler() {
        if (changedEvent_) CloseHandle(changedEvent_);
    }

    HANDLE changedEvent() const { return changedEvent_; }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown ||
            iid == __uuidof(IUIAutomationPropertyChangedEventHandler)) {
            *object = static_cast<IUIAutomationPropertyChangedEventHandler*>(
                this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(
        IUIAutomationElement*, PROPERTYID propertyId, VARIANT) override {
        if (propertyId == UIA_BoundingRectanglePropertyId && changedEvent_) {
            SetEvent(changedEvent_);
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
    HANDLE changedEvent_ = nullptr;
};

HRESULT FindDescendantByAutomationId(IUIAutomation* automation,
                                     IUIAutomationElement* root,
                                     PCWSTR automationId,
                                     IUIAutomationElement** result) {
    if (!automation || !root || !result) return E_INVALIDARG;
    *result = nullptr;
    IUIAutomationCondition* condition = nullptr;
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(automationId);
    if (!value.bstrVal) return E_OUTOFMEMORY;
    HRESULT hr = automation->CreatePropertyCondition(
        UIA_AutomationIdPropertyId, value, &condition);
    if (SUCCEEDED(hr)) {
        hr = root->FindFirst(TreeScope_Descendants, condition, result);
    }
    VariantClear(&value);
    SafeRelease(condition);
    return hr;
}

bool QuerySearchLayout(IUIAutomation* automation, HWND taskbar,
                       IUIAutomationElement** cachedSearch,
                       SearchLayout* result) {
    if (!automation || !taskbar || !cachedSearch || !result) return false;
    IUIAutomationElement* root = nullptr;
    SearchLayout layout;
    layout.taskbar = taskbar;
    GetWindowThreadProcessId(taskbar, &layout.explorerProcessId);
    layout.dpi = GetWindowDpiOrDefault(taskbar);

    HRESULT hr = E_FAIL;
    if (*cachedSearch) {
        hr = (*cachedSearch)->get_CurrentBoundingRectangle(&layout.rect);
        if (FAILED(hr) || IsRectEmpty(&layout.rect)) {
            SafeRelease(*cachedSearch);
        }
    }
    if (!*cachedSearch) {
        hr = automation->ElementFromHandle(taskbar, &root);
        if (SUCCEEDED(hr)) {
            hr = FindDescendantByAutomationId(
                automation, root, L"SearchButton", cachedSearch);
        }
        if (SUCCEEDED(hr) && !*cachedSearch) {
            hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        if (SUCCEEDED(hr)) {
            hr = (*cachedSearch)->get_CurrentBoundingRectangle(&layout.rect);
        }
    }
    if (SUCCEEDED(hr) &&
        (layout.rect.right <= layout.rect.left ||
         layout.rect.bottom <= layout.rect.top)) {
        hr = E_FAIL;
    }

    SafeRelease(root);
    if (FAILED(hr)) return false;
    layout.valid = true;
    *result = layout;
    return true;
}

bool LayoutContentEquals(const SearchLayout& first,
                         const SearchLayout& second) {
    return first.valid == second.valid && first.taskbar == second.taskbar &&
           first.explorerProcessId == second.explorerProcessId &&
           RectEquals(first.rect, second.rect) &&
           first.dpi == second.dpi;
}

void PublishLayout(ApplicationContext& context, SearchLayout layout) {
    auto& state = context.searchLayout;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        changed = !LayoutContentEquals(state.latest, layout);
        if (changed) ++state.generation;
        layout.generation = state.generation;
        state.latest = layout;
    }
    if (changed && context.layoutChangedEvent) {
        SetEvent(context.layoutChangedEvent.get());
    }
}

void RememberLayout(ApplicationContext& context, const SearchLayout& layout) {
    RECT taskbarRect{};
    if (!layout.valid || !layout.taskbar ||
        !GetWindowRect(layout.taskbar, &taskbarRect) ||
        IsRectEmpty(&taskbarRect)) {
        return;
    }
    auto& state = context.searchLayout;
    std::lock_guard<std::mutex> lock(state.mutex);
    state.templateTaskbarRect = taskbarRect;
    state.templateSearchRect = layout.rect;
    state.templateDpi = layout.dpi ? layout.dpi : 96;
    state.templateValid = true;
}

bool BuildLayoutFromTemplate(ApplicationContext& context, HWND taskbar,
                              DWORD explorerProcessId,
                              SearchLayout* result) {
    if (!taskbar || !explorerProcessId || !result ||
        !IsWindow(taskbar) || !IsWindowVisible(taskbar)) {
        return false;
    }
    RECT taskbarRect{};
    if (!GetWindowRect(taskbar, &taskbarRect) || IsRectEmpty(&taskbarRect)) {
        return false;
    }
    RECT templateTaskbarRect{};
    RECT templateSearchRect{};
    UINT templateDpi = 96;
    {
        auto& state = context.searchLayout;
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.templateValid) return false;
        templateTaskbarRect = state.templateTaskbarRect;
        templateSearchRect = state.templateSearchRect;
        templateDpi = state.templateDpi;
    }

    const UINT dpi = GetWindowDpiOrDefault(taskbar);
    const auto scale = [&](LONG value) {
        return MulDiv(value, static_cast<int>(dpi),
                      static_cast<int>(templateDpi));
    };
    const LONG relativeLeft =
        templateSearchRect.left - templateTaskbarRect.left;
    const LONG relativeTop =
        templateSearchRect.top - templateTaskbarRect.top;
    const LONG width = templateSearchRect.right - templateSearchRect.left;
    const LONG height = templateSearchRect.bottom - templateSearchRect.top;
    SearchLayout layout;
    layout.taskbar = taskbar;
    layout.explorerProcessId = explorerProcessId;
    layout.dpi = dpi;
    layout.rect.left = taskbarRect.left + scale(relativeLeft);
    layout.rect.top = taskbarRect.top + scale(relativeTop);
    layout.rect.right = layout.rect.left + scale(width);
    layout.rect.bottom = layout.rect.top + scale(height);
    if (layout.rect.left < taskbarRect.left ||
        layout.rect.top < taskbarRect.top ||
        layout.rect.right > taskbarRect.right ||
        layout.rect.bottom > taskbarRect.bottom ||
        IsRectEmpty(&layout.rect)) {
        return false;
    }
    layout.valid = true;
    *result = layout;
    return true;
}

HRESULT CreateAutomationClient(IUIAutomation2** automation) {
    if (!automation) return E_POINTER;
    *automation = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(automation));
    if (SUCCEEDED(hr)) {
        hr = (*automation)->put_ConnectionTimeout(kUiAutomationTimeoutMs);
    }
    if (SUCCEEDED(hr)) {
        hr = (*automation)->put_TransactionTimeout(kUiAutomationTimeoutMs);
    }
    if (FAILED(hr)) SafeRelease(*automation);
    return hr;
}

bool TryGetSearchMode(DWORD* mode) {
    if (!mode) return false;
    *mode = 0;
    DWORD size = sizeof(*mode);
    return RegGetValueW(
               HKEY_CURRENT_USER,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Search",
               L"SearchboxTaskbarMode", RRF_RT_REG_DWORD, nullptr, mode,
               &size) == ERROR_SUCCESS;
}

DWORD WINAPI SearchLocatorWorkerProc(void* parameter) {
    const std::unique_ptr<LocatorTarget> targetParameter(
        static_cast<LocatorTarget*>(parameter));
    const LocatorTarget target = *targetParameter;
    ApplicationContext& context = *target.context;
    EnablePerMonitorDpiAwarenessForThread();
    const HRESULT initializeResult =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initializeResult)) {
        Log(L"Locator COM initialization failed: 0x%08X", initializeResult);
        return 0;
    }

    const bool cancellationEnabled =
        SUCCEEDED(CoEnableCallCancellation(nullptr));
    IUIAutomation2* automation = nullptr;
    const HRESULT createResult = CreateAutomationClient(&automation);
    if (FAILED(createResult)) {
        Log(L"UI Automation client creation failed: 0x%08X", createResult);
        if (cancellationEnabled) {
            const HRESULT disableResult = CoDisableCallCancellation(nullptr);
            if (FAILED(disableResult)) {
                Log(L"Failed to disable COM call cancellation: 0x%08X",
                    disableResult);
            }
        }
        CoUninitialize();
        return 0;
    }
    Log(L"UI Automation worker started: explorer=%lu timeout=%u ms",
        target.explorerProcessId, kUiAutomationTimeoutMs);

    auto* propertyHandler = new (std::nothrow) LayoutPropertyChangedHandler;
    bool propertyHandlerRegistered = false;

    RegistryChangeWatcher taskbarSettingsWatcher;
    if (context.settings.autoPosition && taskbarSettingsWatcher.Start(
            kExplorerAdvancedRegistryPath)) {
        Log(L"Taskbar settings change watcher started");
    }
    RegistryChangeWatcher searchModeWatcher;
    if (searchModeWatcher.Start(kSearchSettingsRegistryPath)) {
        Log(L"Search mode change watcher started");
    }
    ScopedHandle explorerProcess(OpenProcess(
        SYNCHRONIZE, FALSE, target.explorerProcessId));
    DWORD searchMode = 0;
    bool searchModeKnown = TryGetSearchMode(&searchMode);
    ULONGLONG rapidRefreshUntil = 0;
    SearchLayout previousLayout;
    bool hasPreviousLayout = false;
    IUIAutomationElement* cachedSearch = nullptr;
    int consecutiveFailures = 0;
    bool fallbackLogged = false;
    bool geometryRejectionLogged = false;
    while (WaitForSingleObject(context.stopEvent.get(), 0) != WAIT_OBJECT_0) {
        bool searchBoxGeometry = false;
        const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        DWORD explorerProcessId = 0;
        if (taskbar) {
            GetWindowThreadProcessId(taskbar, &explorerProcessId);
        }
        if (taskbar != target.taskbar ||
            explorerProcessId != target.explorerProcessId) {
            Log(L"UI Automation worker retiring after shell change: old=%lu new=%lu",
                target.explorerProcessId, explorerProcessId);
            break;
        }

        SearchLayout layout;
        IUIAutomationElement* previousSearch = cachedSearch;
        if (previousSearch) previousSearch->AddRef();
        const bool queried = QuerySearchLayout(
            automation, taskbar, &cachedSearch, &layout);
        if (previousSearch != cachedSearch) {
            if (propertyHandlerRegistered && previousSearch) {
                automation->RemovePropertyChangedEventHandler(
                    previousSearch, propertyHandler);
                propertyHandlerRegistered = false;
            }
            if (cachedSearch && propertyHandler &&
                propertyHandler->changedEvent()) {
                PROPERTYID properties[] = {
                    UIA_BoundingRectanglePropertyId};
                propertyHandlerRegistered = SUCCEEDED(
                    automation->AddPropertyChangedEventHandlerNativeArray(
                        cachedSearch, TreeScope_Element, nullptr,
                        propertyHandler, properties,
                        static_cast<int>(ARRAYSIZE(properties))));
            }
        }
        SafeRelease(previousSearch);
        if (queried) {
            const bool layoutChanged = hasPreviousLayout &&
                !LayoutContentEquals(previousLayout, layout);
            previousLayout = layout;
            hasPreviousLayout = true;
            if (context.settings.autoPosition && layoutChanged) {
                rapidRefreshUntil = std::max(
                    rapidRefreshUntil,
                    GetTickCount64() + kRapidLayoutRefreshDurationMs);
            }
            consecutiveFailures = 0;
            fallbackLogged = false;
            if (HasUsableSearchBoxGeometry(layout, context.settings)) {
                searchBoxGeometry = true;
                geometryRejectionLogged = false;
                RememberLayout(context, layout);
                PublishLayout(context, layout);
            } else {
                PublishLayout(context, {});
                if (!geometryRejectionLogged) {
                    const RECT bounds = CalculateSpectrumBounds(
                        layout, context.settings);
                    const LONG drawableWidth = bounds.right - bounds.left;
                    const LONG drawableHeight = bounds.bottom - bounds.top;
                    if (searchModeKnown) {
                        Log(L"Taskbar search control rejected: mode=%lu width=%ld height=%ld drawable=%ldx%ld; overlay idle",
                            searchMode, layout.rect.right - layout.rect.left,
                            layout.rect.bottom - layout.rect.top,
                            drawableWidth, drawableHeight);
                    } else {
                        Log(L"Taskbar search control rejected: mode=unavailable width=%ld height=%ld drawable=%ldx%ld; overlay idle",
                            layout.rect.right - layout.rect.left,
                            layout.rect.bottom - layout.rect.top,
                            drawableWidth, drawableHeight);
                    }
                    geometryRejectionLogged = true;
                }
            }
        } else if (searchModeKnown && searchMode == 2 &&
                   BuildLayoutFromTemplate(
                       context, taskbar, explorerProcessId, &layout)) {
            searchBoxGeometry = true;
            consecutiveFailures = 0;
            PublishLayout(context, layout);
            if (!fallbackLogged) {
                Log(L"Using cached search-box geometry for explorer=%lu",
                    explorerProcessId);
                fallbackLogged = true;
            }
        } else {
            PublishLayout(context, {});
            if (searchModeKnown && searchMode != 2) {
                consecutiveFailures = 0;
            } else if (++consecutiveFailures >= 3) {
                Log(L"UI Automation worker retiring after repeated query failures");
                break;
            }
        }

        const bool rapidRefresh = searchBoxGeometry &&
            context.settings.autoPosition &&
            GetTickCount64() < rapidRefreshUntil;
        const DWORD refreshDelay = rapidRefresh
            ? kRapidLayoutRefreshMs
            : kNormalLayoutRefreshMs;
        HANDLE waits[5] = {context.stopEvent.get()};
        DWORD waitCount = 1;
        DWORD explorerWaitIndex = MAXDWORD;
        DWORD taskbarSettingsWaitIndex = MAXDWORD;
        DWORD searchModeWaitIndex = MAXDWORD;
        DWORD propertyWaitIndex = MAXDWORD;
        if (explorerProcess) {
            explorerWaitIndex = waitCount;
            waits[waitCount++] = explorerProcess.get();
        }
        if (taskbarSettingsWatcher.changedEvent()) {
            taskbarSettingsWaitIndex = waitCount;
            waits[waitCount++] = taskbarSettingsWatcher.changedEvent();
        }
        if (searchModeWatcher.changedEvent()) {
            searchModeWaitIndex = waitCount;
            waits[waitCount++] = searchModeWatcher.changedEvent();
        }
        if (propertyHandler && propertyHandler->changedEvent()) {
            propertyWaitIndex = waitCount;
            waits[waitCount++] = propertyHandler->changedEvent();
        }
        const DWORD waitResult = WaitForMultipleObjects(
            waitCount, waits, FALSE, refreshDelay);
        if (waitResult == WAIT_OBJECT_0) break;
        if (explorerWaitIndex != MAXDWORD &&
            waitResult == WAIT_OBJECT_0 + explorerWaitIndex) {
            Log(L"UI Automation worker retiring after Explorer exited");
            break;
        }
        if (taskbarSettingsWaitIndex != MAXDWORD &&
            waitResult == WAIT_OBJECT_0 + taskbarSettingsWaitIndex) {
            rapidRefreshUntil =
                GetTickCount64() + kRapidLayoutRefreshDurationMs;
            if (!taskbarSettingsWatcher.Arm()) {
                Log(L"Taskbar settings change watcher could not be rearmed");
                taskbarSettingsWatcher.Stop();
            } else {
                Log(L"Taskbar settings changed; refreshing search-box geometry");
            }
        } else if (searchModeWaitIndex != MAXDWORD &&
                   waitResult == WAIT_OBJECT_0 + searchModeWaitIndex) {
            if (!searchModeWatcher.Arm()) {
                Log(L"Search mode change watcher could not be rearmed");
                searchModeWatcher.Stop();
            }
            searchModeKnown = TryGetSearchMode(&searchMode);
            rapidRefreshUntil =
                GetTickCount64() + kRapidLayoutRefreshDurationMs;
            if (searchModeKnown) {
                Log(L"Search mode changed: %lu", searchMode);
            } else {
                Log(L"Search mode changed: value unavailable");
            }
        } else if (propertyWaitIndex != MAXDWORD &&
                   waitResult == WAIT_OBJECT_0 + propertyWaitIndex) {
            rapidRefreshUntil =
                GetTickCount64() + kRapidLayoutRefreshDurationMs;
        } else if (waitResult == WAIT_TIMEOUT) {
            if (!searchModeWatcher.changedEvent()) {
                searchModeKnown = TryGetSearchMode(&searchMode);
            }
        } else {
            Log(L"UI Automation refresh wait failed: %u", GetLastError());
            break;
        }
    }

    PublishLayout(context, {});
    if (propertyHandlerRegistered && cachedSearch) {
        automation->RemovePropertyChangedEventHandler(
            cachedSearch, propertyHandler);
    }
    SafeRelease(cachedSearch);
    if (propertyHandler) propertyHandler->Release();
    SafeRelease(automation);
    if (cancellationEnabled) {
        const HRESULT disableResult = CoDisableCallCancellation(nullptr);
        if (FAILED(disableResult)) {
            Log(L"Failed to disable COM call cancellation: 0x%08X",
                disableResult);
        }
    }
    CoUninitialize();
    Log(L"UI Automation worker stopped: explorer=%lu",
        target.explorerProcessId);
    return 0;
}

}  // namespace

SearchHostKind DetectSearchHostKind(PCWSTR executablePath) {
    if (!executablePath || !*executablePath) return SearchHostKind::Unknown;
    const PCWSTR slash = wcsrchr(executablePath, L'\\');
    const PCWSTR forwardSlash = wcsrchr(executablePath, L'/');
    PCWSTR name = executablePath;
    if (slash && slash + 1 > name) name = slash + 1;
    if (forwardSlash && forwardSlash + 1 > name) name = forwardSlash + 1;
    if (_wcsicmp(name, L"SearchHost.exe") == 0) {
        return SearchHostKind::Windows11;
    }
    if (_wcsicmp(name, L"SearchApp.exe") == 0 ||
        _wcsicmp(name, L"SearchUI.exe") == 0) {
        return SearchHostKind::Windows10;
    }
    return SearchHostKind::Unknown;
}

bool IsSearchExecutableName(PCWSTR executablePath) {
    return DetectSearchHostKind(executablePath) != SearchHostKind::Unknown;
}

bool IsSearchProcessWindow(HWND window) {
    if (!window) return false;
    window = GetAncestor(window, GA_ROOT);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (!processId) return false;
    ScopedHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                     processId));
    if (!process) return false;
    std::array<wchar_t, MAX_PATH> executablePath{};
    DWORD pathLength = static_cast<DWORD>(executablePath.size());
    if (QueryFullProcessImageNameW(
            process.get(), 0, executablePath.data(), &pathLength)) {
        return IsSearchExecutableName(executablePath.data());
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;

    std::vector<wchar_t> longExecutablePath(32768);
    pathLength = static_cast<DWORD>(longExecutablePath.size());
    return QueryFullProcessImageNameW(
               process.get(), 0, longExecutablePath.data(), &pathLength) &&
           IsSearchExecutableName(longExecutablePath.data());
}

bool IsSearchInterfaceOpen() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    foreground = GetAncestor(foreground, GA_ROOT);
    if (!foreground || !IsWindowVisible(foreground) || IsIconic(foreground)) {
        return false;
    }

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(
            foreground, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
        cloaked) {
        return false;
    }

    RECT rectangle{};
    if (!GetWindowRect(foreground, &rectangle) || IsRectEmpty(&rectangle)) {
        return false;
    }
    return IsSearchProcessWindow(foreground);
}

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi ? dpi : 96), 96);
}

RECT CalculateSpectrumBounds(const SearchLayout& layout,
                             const Settings& settings) {
    const int width = layout.rect.right - layout.rect.left;
    const int height = layout.rect.bottom - layout.rect.top;
    const int clampedWidth = std::max(0, width);
    const int clampedHeight = std::max(0, height);
    const int left = std::clamp(
        ScaleForDpi(settings.leftPadding, layout.dpi), 0,
        clampedWidth);
    const int right = std::clamp(
        width - ScaleForDpi(settings.rightPadding, layout.dpi),
        left, clampedWidth);
    const int top = std::clamp(
        ScaleForDpi(settings.topPadding, layout.dpi), 0,
        clampedHeight);
    const int bottom = std::clamp(
        height - ScaleForDpi(settings.bottomPadding, layout.dpi),
        top, clampedHeight);
    return {left, top, right, bottom};
}

bool HasUsableSearchBoxGeometry(const SearchLayout& layout,
                                const Settings& settings) {
    if (!layout.valid || IsRectEmpty(&layout.rect)) return false;
    const LONG width = layout.rect.right - layout.rect.left;
    const LONG height = layout.rect.bottom - layout.rect.top;
    if (width < height * 3) return false;
    const RECT bounds = CalculateSpectrumBounds(layout, settings);
    return bounds.right - bounds.left >= ScaleForDpi(64, layout.dpi) &&
           bounds.bottom > bounds.top;
}

bool RectEquals(const RECT& first, const RECT& second) {
    return first.left == second.left && first.top == second.top &&
           first.right == second.right && first.bottom == second.bottom;
}

bool ReadLatestSearchLayout(ApplicationContext& context,
                            SearchLayout* layout) {
    if (!layout) return false;
    auto& state = context.searchLayout;
    std::lock_guard<std::mutex> lock(state.mutex);
    *layout = state.latest;
    return layout->valid;
}

bool IsFullscreenForeground(const SearchLayout& layout) {
    HWND foreground = GetForegroundWindow();
    if (!foreground || IsIconic(foreground)) return false;
    foreground = GetAncestor(foreground, GA_ROOT);
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    if (!processId || processId == layout.explorerProcessId) return false;

    RECT foregroundRect{};
    if (!GetWindowRect(foreground, &foregroundRect)) return false;
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const HMONITOR monitor = MonitorFromRect(
        &layout.rect, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return false;
    constexpr LONG tolerance = 2;
    return foregroundRect.left <= monitorInfo.rcMonitor.left + tolerance &&
           foregroundRect.top <= monitorInfo.rcMonitor.top + tolerance &&
           foregroundRect.right >= monitorInfo.rcMonitor.right - tolerance &&
           foregroundRect.bottom >= monitorInfo.rcMonitor.bottom - tolerance;
}

bool ShouldShowOverlay(const SearchLayout& layout, bool searchInterfaceOpen,
                       bool fullscreenForeground) {
    if (!layout.valid || !layout.taskbar || !IsWindow(layout.taskbar) ||
        !IsWindowVisible(layout.taskbar) ||
        IsRectEmpty(&layout.rect) || searchInterfaceOpen ||
        fullscreenForeground) {
        return false;
    }
    DWORD currentProcessId = 0;
    GetWindowThreadProcessId(layout.taskbar, &currentProcessId);
    return currentProcessId != 0 &&
           currentProcessId == layout.explorerProcessId;
}

DWORD WINAPI SearchLocatorThreadProc(void* parameter) {
    auto& context = *static_cast<ApplicationContext*>(parameter);
    const HRESULT initializeResult =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initializeResult)) {
        Log(L"Locator supervisor COM initialization failed: 0x%08X",
            initializeResult);
        PublishLayout(context, {});
        return 0;
    }
    Log(L"UI Automation locator supervisor started");
    while (WaitForSingleObject(context.stopEvent.get(), 0) != WAIT_OBJECT_0) {
        const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!taskbar) {
            PublishLayout(context, {});
            if (WaitForSingleObject(context.stopEvent.get(), 500) ==
                WAIT_OBJECT_0) break;
            continue;
        }
        DWORD explorerProcessId = 0;
        GetWindowThreadProcessId(taskbar, &explorerProcessId);
        if (!explorerProcessId) {
            if (WaitForSingleObject(context.stopEvent.get(), 500) ==
                WAIT_OBJECT_0) break;
            continue;
        }

        auto target =
            std::unique_ptr<LocatorTarget>(new (std::nothrow) LocatorTarget);
        if (!target) {
            Log(L"Failed to allocate UI Automation worker target");
            break;
        }
        target->context = &context;
        target->taskbar = taskbar;
        target->explorerProcessId = explorerProcessId;
        DWORD workerThreadId = 0;
        HANDLE worker = CreateThread(
            nullptr, 0, SearchLocatorWorkerProc, target.get(), 0,
            &workerThreadId);
        if (!worker) {
            Log(L"Failed to create UI Automation worker: %u", GetLastError());
            if (WaitForSingleObject(context.stopEvent.get(), 750) ==
                WAIT_OBJECT_0) break;
            continue;
        }
        target.release();

        HANDLE waits[] = {context.stopEvent.get(), worker};
        const DWORD waitResult = WaitForMultipleObjects(
            ARRAYSIZE(waits), waits, FALSE, INFINITE);
        if (waitResult == WAIT_OBJECT_0) {
            if (WaitForSingleObject(worker, 2500) == WAIT_TIMEOUT) {
                const HRESULT cancelResult = CoCancelCall(workerThreadId, 1);
                if (SUCCEEDED(cancelResult)) {
                    Log(L"Cancellation requested for UI Automation worker");
                } else {
                    Log(L"Failed to cancel UI Automation worker: 0x%08X",
                        cancelResult);
                }
                if (WaitForSingleObject(worker, 2500) == WAIT_TIMEOUT) {
                    context.locatorShutdownIncomplete.store(
                        true, std::memory_order_release);
                    Log(L"UI Automation worker did not stop within 5000 ms");
                }
            }
            CloseHandle(worker);
            break;
        }
        if (waitResult != WAIT_OBJECT_0 + 1) {
            Log(L"UI Automation supervisor wait failed: %u", GetLastError());
            context.locatorShutdownIncomplete.store(
                true, std::memory_order_release);
            CloseHandle(worker);
            break;
        }
        CloseHandle(worker);
        if (WaitForSingleObject(context.stopEvent.get(), 100) ==
            WAIT_OBJECT_0) break;
    }

    PublishLayout(context, {});
    Log(L"UI Automation locator supervisor stopped");
    CoUninitialize();
    return 0;
}

}  // namespace tas
