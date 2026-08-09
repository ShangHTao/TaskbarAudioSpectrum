#include <taskbar_audio_spectrum/application.h>
#include <taskbar_audio_spectrum/host.h>
#include <taskbar_audio_spectrum/json_config.h>
#include <taskbar_audio_spectrum/settings.h>
#include <taskbar_audio_spectrum/standalone_host.h>

#ifndef TAS_WINDHAWK_HOST

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace tas {

Runtime& AppRuntime() {
    static Runtime runtime;
    return runtime;
}

namespace {

struct StandaloneHostState {
    std::wstring configPath;
    std::wstring logPath;
    std::wstring executablePath;
    std::mutex logMutex;
    JsonConfig config;
};

StandaloneHostState& HostState() {
    static StandaloneHostState state;
    return state;
}

struct Mapping {
    PCWSTR setting;
    PCWSTR path;
    JsonValueKind kind;
};

constexpr Mapping kMappings[] = {
    {L"fftSize", L"audio.fftSize", JsonValueKind::Number},
    {L"overlapPercent", L"audio.overlapPercent", JsonValueKind::Number},
    {L"windowFunction", L"audio.windowFunction", JsonValueKind::String},
    {L"nonOctaveBarCount", L"spectrum.bands", JsonValueKind::Number},
    {L"minimumFrequency", L"spectrum.minFrequency", JsonValueKind::Number},
    {L"maximumFrequency", L"spectrum.maxFrequency", JsonValueKind::Number},
    {L"minimumCenterFrequency", L"spectrum.minCenterFrequency", JsonValueKind::Number},
    {L"maximumCenterFrequency", L"spectrum.maxCenterFrequency", JsonValueKind::Number},
    {L"referenceFrequency", L"spectrum.referenceFrequency", JsonValueKind::Number},
    {L"frequencyScale", L"spectrum.frequencyScale", JsonValueKind::String},
    {L"bandAggregation", L"spectrum.bandAggregation", JsonValueKind::String},
    {L"frequencyWeighting", L"spectrum.frequencyWeighting", JsonValueKind::String},
    {L"foldBelowMinimum", L"spectrum.foldBelowMinimum", JsonValueKind::Boolean},
    {L"sensitivity", L"spectrum.sensitivity", JsonValueKind::Number},
    {L"minimumDecibels", L"spectrum.minimumDecibels", JsonValueKind::Number},
    {L"maximumDecibels", L"spectrum.maximumDecibels", JsonValueKind::Number},
    {L"attackMs", L"spectrum.attackMs", JsonValueKind::Number},
    {L"releaseMs", L"spectrum.releaseMs", JsonValueKind::Number},
    {L"framesPerSecond", L"display.framesPerSecond", JsonValueKind::Number},
    {L"barColor", L"display.barColor", JsonValueKind::String},
    {L"secondColor", L"display.secondColor", JsonValueKind::String},
    {L"opacity", L"display.opacity", JsonValueKind::Number},
    {L"hideWhenSilent", L"silence.enabled", JsonValueKind::Boolean},
    {L"silenceThreshold", L"silence.threshold", JsonValueKind::Number},
    {L"silenceHideDelayMs", L"silence.hideDelayMs", JsonValueKind::Number},
    {L"peakEnabled", L"peak.enabled", JsonValueKind::Boolean},
    {L"peakShowWhenSilent", L"peak.showWhenSilent", JsonValueKind::Boolean},
    {L"peakHoldMs", L"peak.holdMs", JsonValueKind::Number},
    {L"peakGravity", L"peak.gravity", JsonValueKind::Number},
    {L"peakHeight", L"peak.height", JsonValueKind::Number},
    {L"peakGap", L"peak.gap", JsonValueKind::Number},
    {L"bottomPadding", L"position.bottomOffset", JsonValueKind::Number},
    {L"topPadding", L"position.topOffset", JsonValueKind::Number},
    {L"rightPadding", L"position.rightOffset", JsonValueKind::Number},
    {L"barWidthPercent", L"display.barWidthPercent", JsonValueKind::Number},
    {L"auto", L"position.automatic", JsonValueKind::Boolean},
    {L"autoPosition", L"position.automatic", JsonValueKind::Boolean},
    {L"spectrumLeftOffset", L"position.leftOffset", JsonValueKind::Number},
    {L"startWithWindows", L"startup.startWithWindows", JsonValueKind::Boolean},
};

struct LegacyPath {
    PCWSTR oldPath;
    PCWSTR newPath;
};

constexpr LegacyPath kLegacyPaths[] = {
    {L"display.hideWhenSilent", L"silence.enabled"},
    {L"display.bottomPadding", L"position.bottomOffset"},
    {L"display.topPadding", L"position.topOffset"},
    {L"display.rightPadding", L"position.rightOffset"},
};

PCWSTR JsonPathForSetting(PCWSTR key) {
    if (!key) return nullptr;
    for (const auto& mapping : kMappings) {
        if (_wcsicmp(key, mapping.setting) == 0) return mapping.path;
    }
    return nullptr;
}

const Mapping* FindMappingByPath(const std::wstring& path) {
    for (const auto& mapping : kMappings) {
        if (path == mapping.path) return &mapping;
    }
    return nullptr;
}

bool IsKnownObjectPath(const std::wstring& path) {
    constexpr PCWSTR kObjects[] = {
        L"audio", L"spectrum", L"peak", L"silence", L"display",
        L"position", L"startup"};
    return std::any_of(std::begin(kObjects), std::end(kObjects),
                       [&](PCWSTR object) { return path == object; });
}

PCWSTR JsonValueKindName(JsonValueKind kind) {
    switch (kind) {
        case JsonValueKind::Object: return L"object";
        case JsonValueKind::String: return L"string";
        case JsonValueKind::Number: return L"number";
        case JsonValueKind::Boolean: return L"boolean";
        default: return L"null";
    }
}

PCWSTR ReplacementForLegacyPath(const std::wstring& path) {
    for (const auto& legacy : kLegacyPaths) {
        if (path == legacy.oldPath) return legacy.newPath;
    }
    return nullptr;
}

bool ReadJsonSetting(PCWSTR key, std::wstring* value) {
    const PCWSTR path = JsonPathForSetting(key);
    if (path && HostState().config.Get(path, value)) return true;
    return false;
}

}  // namespace

bool InitializeStandaloneHost(PCWSTR executablePath) {
    auto& state = HostState();
    state.executablePath = executablePath ? executablePath : L"";
    const size_t separator = state.executablePath.find_last_of(L"\\/");
    const std::wstring directory = separator == std::wstring::npos
        ? L""
        : state.executablePath.substr(0, separator + 1);
    state.configPath = directory + L"spectrum.json";
    state.logPath = directory + L"spectrum.log";
    std::wstring error;
    if (!state.config.LoadFile(state.configPath.c_str(), &error)) {
        const DWORD attributes = GetFileAttributesW(state.configPath.c_str());
        const DWORD attributeError = attributes == INVALID_FILE_ATTRIBUTES
            ? GetLastError()
            : ERROR_SUCCESS;
        if (attributeError == ERROR_FILE_NOT_FOUND ||
            attributeError == ERROR_PATH_NOT_FOUND) {
            Log(L"Configuration not found: %ls; using defaults",
                state.configPath.c_str());
            return true;
        }
        Log(L"Configuration load failed: %ls (%ls)",
            state.configPath.c_str(), error.c_str());
        return false;
    }

    Log(L"Configuration loaded: %ls", state.configPath.c_str());
    std::wstring validationError;
    if (!ValidateStandaloneConfig(state.config, &validationError)) {
        Log(L"Configuration schema validation failed: %ls",
            validationError.c_str());
        return false;
    }
    return true;
}

bool ValidateStandaloneConfig(const JsonConfig& config, std::wstring* error) {
    std::wstring messages;
    for (const JsonEntry& entry : config.Entries()) {
        const std::wstring& path = entry.path;
        std::wstring message;
        if (const PCWSTR replacement = ReplacementForLegacyPath(path)) {
            message = L"Legacy setting '" + path + L"'; use '" +
                      replacement + L"'";
        } else if (path == L"_comment") {
            if (entry.kind != JsonValueKind::String) {
                message = L"Setting '_comment' must be a string";
            }
        } else if (entry.kind == JsonValueKind::Object) {
            if (!IsKnownObjectPath(path)) {
                message = L"Unknown setting group '" + path + L"'";
            }
        } else if (const Mapping* mapping = FindMappingByPath(path)) {
            if (entry.kind != mapping->kind) {
                message = L"Setting '" + path + L"' must be a " +
                          JsonValueKindName(mapping->kind) + L", not " +
                          JsonValueKindName(entry.kind);
            }
        } else {
            message = L"Unknown setting '" + path + L"'";
        }
        if (!message.empty()) {
            if (!messages.empty()) messages += L"; ";
            messages += message;
        }
    }
    if (error) *error = messages;
    return messages.empty();
}

void Log(PCWSTR format, ...) {
    auto& state = HostState();
    std::lock_guard<std::mutex> lock(state.logMutex);
    wchar_t message[768]{};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, format, arguments);
    va_end(arguments);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t line[1024]{};
    _snwprintf_s(
        line, ARRAYSIZE(line), _TRUNCATE,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u [pid=%lu host=standalone] %ls",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds, GetCurrentProcessId(), message);
    OutputDebugStringW(line);
    OutputDebugStringW(L"\n");

    if (state.logPath.empty()) return;
    FILE* file = nullptr;
    if (_wfopen_s(&file, state.logPath.c_str(), L"a, ccs=UTF-8") == 0 &&
        file) {
        fwprintf(file, L"%ls\n", line);
        fclose(file);
    }
}

int HostGetIntSetting(PCWSTR key, int fallback) {
    std::wstring value;
    if (!ReadJsonSetting(key, &value)) return fallback;
    bool boolean = false;
    if (TryParseAutoMode(value.c_str(), &boolean)) return boolean ? 1 : 0;
    int result = 0;
    if (TryParseClampedInt(value.c_str(), INT_MIN, INT_MAX, &result)) {
        return result;
    }
    Log(L"Invalid JSON integer for %ls: '%ls'; using %d",
        key, value.c_str(), fallback);
    return fallback;
}

std::wstring HostGetStringSetting(PCWSTR key, PCWSTR fallback) {
    std::wstring value;
    return ReadJsonSetting(key, &value) ? value : fallback;
}

bool HostReadRawSetting(PCWSTR key, std::wstring* value) {
    return value && ReadJsonSetting(key, value);
}

bool HostSupportsStartup() {
    return true;
}

bool HostSyncStartup(bool enabled) {
    constexpr PCWSTR kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr PCWSTR kValueName = L"TaskbarAudioSpectrum";
    auto& state = HostState();
    if (enabled && state.executablePath.empty()) {
        Log(L"Cannot enable Windows startup: executable path is unavailable");
        return false;
    }

    HKEY key = nullptr;
    LSTATUS status = enabled
        ? RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                          KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr)
        : RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
    if (!enabled && status == ERROR_FILE_NOT_FOUND) return true;
    if (status != ERROR_SUCCESS) {
        Log(L"Failed to open Windows startup registry key: %ld", status);
        return false;
    }

    bool success = true;
    if (enabled) {
        const std::wstring command =
            StartupCommandForExecutable(state.executablePath.c_str());
        std::vector<wchar_t> existing(32768);
        DWORD type = 0;
        DWORD bytes = static_cast<DWORD>(existing.size() * sizeof(wchar_t));
        const LSTATUS query = RegQueryValueExW(
            key, kValueName, nullptr, &type,
            reinterpret_cast<BYTE*>(existing.data()), &bytes);
        const bool terminated =
            bytes >= sizeof(wchar_t) && bytes % sizeof(wchar_t) == 0 &&
            bytes / sizeof(wchar_t) <= existing.size() &&
            existing[bytes / sizeof(wchar_t) - 1] == L'\0';
        const bool current = query == ERROR_SUCCESS && type == REG_SZ &&
                             terminated &&
                             wcscmp(existing.data(), command.c_str()) == 0;
        if (!current) {
            status = RegSetValueExW(
                key, kValueName, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            success = status == ERROR_SUCCESS;
            if (success) {
                Log(L"Windows startup enabled: %ls", command.c_str());
            } else {
                Log(L"Failed to enable Windows startup: %ld", status);
            }
        }
    } else {
        status = RegDeleteValueW(key, kValueName);
        success = status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
        if (status == ERROR_SUCCESS) Log(L"Windows startup disabled");
        if (!success) Log(L"Failed to disable Windows startup: %ld", status);
    }
    RegCloseKey(key);
    return success;
}

}  // namespace tas

#endif
