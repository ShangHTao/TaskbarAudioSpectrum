#pragma once

#include "settings.h"

#include <memory>

namespace tas {
struct ApplicationContext;

class Runtime {
public:
    Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    ~Runtime();

    bool Start(const Settings& settings);
    bool Stop();
    bool running() const;

private:
    ScopedHandle audioThread_;
    ScopedHandle locatorThread_;
    ScopedHandle overlayThread_;
    DWORD overlayThreadId_ = 0;
    std::unique_ptr<ApplicationContext> context_;
};

Runtime& AppRuntime();
}  // namespace tas
