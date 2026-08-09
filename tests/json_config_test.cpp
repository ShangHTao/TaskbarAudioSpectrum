#include <taskbar_audio_spectrum/json_config.h>

#include "test_support.h"

int main() {
    TestRunner test;
    tas::JsonConfig config;
    std::wstring error;
    std::wstring value;

    test.Check(config.Parse(LR"({"audio":{"fftSize":8192},"enabled":true})",
                            &error),
               "nested object parsing");
    test.Check(config.Get(L"audio.fftSize", &value) && value == L"8192",
               "dotted path lookup");
    test.Check(config.Get(L"enabled", &value) && value == L"true",
               "boolean normalization");
    test.Check(!config.Parse(LR"({"value":1,"value":2})", &error) &&
                   error.find(L"duplicate") != std::wstring::npos,
               "duplicate path rejection");
    test.Check(!config.Parse(LR"({"items":[1]})", &error),
               "array rejection");
    test.Check(!config.Parse(LR"({"value":1,})", &error),
               "trailing comma rejection");
    return test.Finish();
}
