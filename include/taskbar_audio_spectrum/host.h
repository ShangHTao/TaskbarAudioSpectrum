#pragma once

#include "platform.h"

namespace tas {
// TAS_WINDHAWK_EXCLUDE_BEGIN
void Log(PCWSTR format, ...);
// TAS_WINDHAWK_EXCLUDE_END
int HostGetIntSetting(PCWSTR key, int fallback);
std::wstring HostGetStringSetting(PCWSTR key, PCWSTR fallback);
// TAS_WINDHAWK_EXCLUDE_BEGIN
bool HostReadRawSetting(PCWSTR key, std::wstring* value);
bool HostSyncStartup(bool enabled);
bool HostSupportsStartup();
// TAS_WINDHAWK_EXCLUDE_END
}  // namespace tas
