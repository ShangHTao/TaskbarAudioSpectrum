#include <taskbar_audio_spectrum/application.h>
#include <taskbar_audio_spectrum/host.h>
#include <taskbar_audio_spectrum/standalone_host.h>
#include <taskbar_audio_spectrum/settings.h>

#ifndef TAS_WINDHAWK_HOST

namespace tas {
namespace {

std::wstring CurrentExecutablePath() {
    std::vector<wchar_t> path(512);
    while (true) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length) return {};
        if (length < path.size() - 1) return {path.data(), length};
        path.resize(path.size() * 2);
    }
}

LRESULT CALLBACK ControllerWindowProc(HWND window, UINT message,
                                      WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(window);
    UNREFERENCED_PARAMETER(wParam);
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wParam) Log(L"Windows session is ending");
            return 0;
        case WM_DESTROY:
            AppRuntime().Stop();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace
}  // namespace tas

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE,
                    _In_ PWSTR commandLine, _In_ int) {
    using namespace tas;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const bool exitRequested = TokenEquals(commandLine, L"--exit");

    ScopedHandle exitEvent(CreateEventW(
        nullptr, FALSE, FALSE, L"Local\\TaskbarAudioSpectrumExit"));
    if (!exitEvent) return 1;
    ScopedHandle singleInstance(CreateMutexW(
        nullptr, TRUE, L"Local\\TaskbarAudioSpectrumStandalone"));
    if (!singleInstance) {
        return 1;
    }
    const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
    bool ownsSingleInstance = !alreadyRunning;
    const auto closeControlHandles = [&] {
        if (ownsSingleInstance) ReleaseMutex(singleInstance.get());
    };

    if (exitRequested) {
        if (alreadyRunning) SetEvent(exitEvent.get());
        closeControlHandles();
        return 0;
    }

    bool replacedExistingInstance = false;
    if (alreadyRunning) {
        if (!SetEvent(exitEvent.get())) {
            closeControlHandles();
            return 1;
        }
        const DWORD takeoverResult =
            WaitForSingleObject(singleInstance.get(), kInstanceTakeoverTimeout);
        if (!InstanceTakeoverSucceeded(takeoverResult)) {
            MessageBoxW(
                nullptr,
                L"The previous instance did not stop within 15 seconds. "
                L"End its TaskbarAudioSpectrum process and try again.",
                L"Taskbar Audio Spectrum", MB_OK | MB_ICONERROR);
            closeControlHandles();
            return 1;
        }
        ownsSingleInstance = true;
        replacedExistingInstance = true;
    }

    const std::wstring executablePath = CurrentExecutablePath();
    if (executablePath.empty()) {
        closeControlHandles();
        return 1;
    }
    if (!InitializeStandaloneHost(executablePath.c_str())) {
        MessageBoxW(nullptr,
                    L"spectrum.json is invalid. See spectrum.log for details.",
                    L"Taskbar Audio Spectrum", MB_OK | MB_ICONERROR);
        closeControlHandles();
        return 1;
    }
    Log(L"Application starting: reason=%ls executable=%ls",
        replacedExistingInstance ? L"instance-takeover" : L"normal",
        executablePath.c_str());
    const Settings settings = LoadSettings();
    if (!AppRuntime().Start(settings)) {
        Log(L"Runtime initialization failed");
        MessageBoxW(nullptr, L"The audio spectrum runtime could not start.",
                    L"Taskbar Audio Spectrum", MB_OK | MB_ICONERROR);
        closeControlHandles();
        return 1;
    }

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = tas::ControllerWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"TaskbarAudioSpectrumController";
    if (!RegisterClassExW(&windowClass)) {
        AppRuntime().Stop();
        closeControlHandles();
        return 1;
    }
    HWND window = CreateWindowExW(
        0, windowClass.lpszClassName, L"Taskbar Audio Spectrum",
        WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) {
        AppRuntime().Stop();
        UnregisterClassW(windowClass.lpszClassName, instance);
        closeControlHandles();
        return 1;
    }

    bool running = true;
    while (running) {
        HANDLE waits[] = {exitEvent.get()};
        const DWORD waitResult = MsgWaitForMultipleObjectsEx(
            ARRAYSIZE(waits), waits, INFINITE, QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (waitResult == WAIT_OBJECT_0) {
            Log(L"Exit requested by another instance");
            running = false;
        } else if (waitResult == WAIT_OBJECT_0 + ARRAYSIZE(waits)) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else {
            Log(L"Controller message wait failed: %u", GetLastError());
            running = false;
        }
    }

    if (IsWindow(window)) DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    closeControlHandles();
    Log(L"Application exiting");
    return 0;
}

#endif
