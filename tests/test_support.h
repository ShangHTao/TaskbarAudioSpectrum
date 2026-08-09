#pragma once

#include <iostream>

class TestRunner {
public:
    void Check(bool condition, const char* description) {
        if (condition) {
            std::cout << "PASS: " << description << '\n';
        } else {
            std::cerr << "FAIL: " << description << '\n';
            ++failures_;
        }
    }

    int Finish() const {
        std::cout << (failures_ ? "TEST FAILED" : "TEST PASSED") << '\n';
        return failures_ ? 1 : 0;
    }

private:
    int failures_ = 0;
};
