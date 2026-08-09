namespace tas {

void Log(PCWSTR format, ...) {
    wchar_t message[768]{};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    Wh_Log(L"[pid=%lu host=windhawk] %ls", GetCurrentProcessId(), message);
}

int HostGetIntSetting(PCWSTR key, int) {
    return Wh_GetIntSetting(key);
}

std::wstring HostGetStringSetting(PCWSTR key, PCWSTR) {
    const auto value = WindhawkUtils::StringSetting::make(key);
    return value.get();
}

}  // namespace tas

BOOL WhTool_ModInit() {
    tas::Log(L"Windhawk host starting");
    return tas::AppRuntime().Start(tas::LoadSettings()) ? TRUE : FALSE;
}

void WhTool_ModSettingsChanged() {
    if (!tas::AppRuntime().Stop()) {
        tas::Log(L"Stale runtime state was abandoned after shutdown timed out");
    }
    if (!tas::AppRuntime().Start(tas::LoadSettings())) {
        tas::Log(L"Runtime restart failed after settings changed");
    }
}

void WhTool_ModUninit() {
    tas::AppRuntime().Stop();
    tas::Log(L"Windhawk host stopped");
}
