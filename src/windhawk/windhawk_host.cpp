namespace {

[[clang::no_destroy]] std::optional<tas::Runtime> g_appRuntime;

}  // namespace

namespace tas {

Runtime& AppRuntime() {
    if (!g_appRuntime) g_appRuntime.emplace();
    return *g_appRuntime;
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
    Wh_Log(L"Windhawk host starting");
    return tas::AppRuntime().Start(tas::LoadSettings()) ? TRUE : FALSE;
}

void WhTool_ModSettingsChanged() {
    if (!tas::AppRuntime().Stop()) {
        Wh_Log(L"Stale runtime state was abandoned after shutdown timed out");
    }
    if (!tas::AppRuntime().Start(tas::LoadSettings())) {
        Wh_Log(L"Runtime restart failed after settings changed");
    }
}

void WhTool_ModUninit() {
    g_appRuntime.reset();
    Wh_Log(L"Windhawk host stopped");
}
