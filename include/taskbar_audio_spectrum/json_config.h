#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tas {
enum class JsonValueKind { Object, String, Number, Boolean, Null };

struct JsonEntry {
    std::wstring path;
    JsonValueKind kind = JsonValueKind::Null;
};

class JsonConfig {
public:
    bool LoadFile(const wchar_t* path, std::wstring* error);
    bool Parse(std::wstring_view text, std::wstring* error);
    bool Get(std::wstring_view path, std::wstring* value) const;
    std::vector<std::wstring> Paths() const;
    std::vector<JsonEntry> Entries() const;
private:
    std::unordered_map<std::wstring, std::wstring> values_;
    std::unordered_map<std::wstring, JsonValueKind> kinds_;
};
}  // namespace tas
