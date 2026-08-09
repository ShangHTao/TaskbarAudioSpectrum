#pragma once

#include "platform.h"
#include "json_config.h"

namespace tas {
// Initializes paths relative to the executable and validates spectrum.json.
// Returns false when the file exists but does not match the canonical schema.
bool InitializeStandaloneHost(PCWSTR executablePath);
bool ValidateStandaloneConfig(const JsonConfig& config, std::wstring* error);
}  // namespace tas
