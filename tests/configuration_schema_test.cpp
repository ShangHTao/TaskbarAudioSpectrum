#include <taskbar_audio_spectrum/json_config.h>
#include <taskbar_audio_spectrum/standalone_host.h>

#include "test_support.h"

int main() {
    TestRunner test;
    tas::JsonConfig config;
    std::wstring error;

    test.Check(config.Parse(
                   LR"({"audio":{"fftSize":8192},"_comment":"ok"})",
                   &error) &&
                   tas::ValidateStandaloneConfig(config, &error),
               "canonical schema acceptance");
    test.Check(config.Parse(
                   LR"({"audio":{"fftSzie":8192}})", &error) &&
                   !tas::ValidateStandaloneConfig(config, &error) &&
                   error.find(L"Unknown setting") != std::wstring::npos,
               "unknown path rejection");
    test.Check(config.Parse(LR"({"audio":{"fftSize":"8192"}})", &error) &&
                   !tas::ValidateStandaloneConfig(config, &error) &&
                   error.find(L"must be a number") != std::wstring::npos,
               "incorrect value type rejection");
    test.Check(config.Parse(LR"({"unknown":null})", &error) &&
                   !tas::ValidateStandaloneConfig(config, &error) &&
                   error.find(L"Unknown setting") != std::wstring::npos,
               "unknown null path rejection");
    test.Check(config.Parse(LR"({"unknown":{}})", &error) &&
                   !tas::ValidateStandaloneConfig(config, &error) &&
                   error.find(L"Unknown setting group") != std::wstring::npos,
               "unknown object rejection");
    return test.Finish();
}
