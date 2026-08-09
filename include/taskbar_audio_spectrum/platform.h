#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace tas {

constexpr int kMaxBars = 64;
constexpr int kDefaultFftSamples = 16384;
constexpr int kDefaultFftOverlapPercent = 75;
// TAS_WINDHAWK_EXCLUDE_BEGIN
constexpr DWORD kInstanceTakeoverTimeout = 15000;
// TAS_WINDHAWK_EXCLUDE_END
constexpr DWORD kUiAutomationTimeoutMs = 2000;
constexpr wchar_t kOverlayWindowClass[] = L"TaskbarAudioSpectrumOverlay";
constexpr double kPi = 3.14159265358979323846;

class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
    ~ScopedHandle() { reset(); }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.release()) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    HANDLE get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
    HANDLE release() {
        HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }
    void reset(HANDLE handle = nullptr) {
        if (handle_) CloseHandle(handle_);
        handle_ = handle;
    }

private:
    HANDLE handle_ = nullptr;
};

template <typename T>
void SafeRelease(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

inline void EnablePerMonitorDpiAwarenessForThread() {
    using SetThreadDpiAwarenessContextFunction = HANDLE(WINAPI*)(HANDLE);
    static const auto setThreadDpiAwarenessContext = [] {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<SetThreadDpiAwarenessContextFunction>(
                            GetProcAddress(user32,
                                           "SetThreadDpiAwarenessContext"))
                      : nullptr;
    }();
    if (setThreadDpiAwarenessContext) {
        setThreadDpiAwarenessContext(
            reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)));
    }
}

}  // namespace tas
