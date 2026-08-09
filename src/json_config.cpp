#include <taskbar_audio_spectrum/json_config.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <limits>
#include <unordered_set>

namespace tas {
namespace {

constexpr DWORD kMaximumConfigBytes = 1024 * 1024;
constexpr int kMaximumNestingDepth = 32;

class Parser {
public:
    Parser(std::wstring_view text,
           std::unordered_map<std::wstring, std::wstring>* values,
           std::unordered_map<std::wstring, JsonValueKind>* kinds)
        : text_(text), values_(values), kinds_(kinds) {}

    bool Run(std::wstring* error) {
        SkipWhitespace();
        if (!ParseObject(L"", 0)) return FinishError(error);
        SkipWhitespace();
        if (position_ != text_.size()) {
            Fail(L"unexpected content after the root object");
            return FinishError(error);
        }
        return true;
    }

private:
    void SkipWhitespace() {
        while (position_ < text_.size()) {
            const wchar_t character = text_[position_];
            if (character != L' ' && character != L'\t' &&
                character != L'\r' && character != L'\n') {
                break;
            }
            ++position_;
        }
    }

    bool Consume(wchar_t expected) {
        SkipWhitespace();
        if (position_ >= text_.size() || text_[position_] != expected) {
            std::wstring message = L"expected '";
            message.push_back(expected);
            message += L"'";
            return Fail(message);
        }
        ++position_;
        return true;
    }

    bool ParseObject(const std::wstring& prefix, int depth) {
        if (depth > kMaximumNestingDepth) {
            return Fail(L"object nesting is too deep");
        }
        if (!Consume(L'{')) return false;
        SkipWhitespace();
        if (position_ < text_.size() && text_[position_] == L'}') {
            ++position_;
            return true;
        }

        std::unordered_set<std::wstring> keys;
        while (position_ < text_.size()) {
            std::wstring key;
            if (!ParseString(&key)) return false;
            if (!keys.insert(key).second) {
                return Fail(L"duplicate object key '" + key + L"'");
            }
            if (!Consume(L':')) return false;
            const std::wstring path = prefix.empty()
                ? key
                : prefix + L"." + key;
            if (!ParseValue(path, depth + 1)) return false;

            SkipWhitespace();
            if (position_ >= text_.size()) {
                return Fail(L"unterminated object");
            }
            if (text_[position_] == L'}') {
                ++position_;
                return true;
            }
            if (text_[position_] != L',') {
                return Fail(L"expected ',' or '}'");
            }
            ++position_;
            SkipWhitespace();
        }
        return Fail(L"unterminated object");
    }

    bool ParseValue(const std::wstring& path, int depth) {
        SkipWhitespace();
        if (position_ >= text_.size()) return Fail(L"missing value");
        if (text_[position_] == L'{') {
            if (!Record(path, JsonValueKind::Object, L"")) return false;
            return ParseObject(path, depth);
        }
        if (text_[position_] == L'[') {
            return Fail(L"arrays are not supported in the configuration");
        }

        std::wstring value;
        JsonValueKind kind = JsonValueKind::Null;
        if (text_[position_] == L'\"') {
            if (!ParseString(&value)) return false;
            kind = JsonValueKind::String;
        } else if (text_[position_] == L'-' ||
                   iswdigit(text_[position_])) {
            if (!ParseNumber(&value)) return false;
            kind = JsonValueKind::Number;
        } else if (ConsumeLiteral(L"true")) {
            value = L"true";
            kind = JsonValueKind::Boolean;
        } else if (ConsumeLiteral(L"false")) {
            value = L"false";
            kind = JsonValueKind::Boolean;
        } else if (ConsumeLiteral(L"null")) {
            kind = JsonValueKind::Null;
        } else {
            return Fail(L"invalid value");
        }

        return Record(path, kind, std::move(value));
    }

    bool Record(const std::wstring& path, JsonValueKind kind,
                std::wstring value) {
        if (!kinds_->emplace(path, kind).second) {
            return Fail(L"duplicate setting path '" + path + L"'");
        }
        if (kind != JsonValueKind::Object && kind != JsonValueKind::Null) {
            values_->emplace(path, std::move(value));
        }
        return true;
    }

    bool ParseString(std::wstring* value) {
        SkipWhitespace();
        if (position_ >= text_.size() || text_[position_] != L'\"') {
            return Fail(L"expected a JSON string");
        }
        ++position_;
        value->clear();
        while (position_ < text_.size()) {
            wchar_t character = text_[position_++];
            if (character == L'\"') return true;
            if (character < 0x20) {
                return Fail(L"unescaped control character in string");
            }
            if (character != L'\\') {
                value->push_back(character);
                continue;
            }
            if (position_ >= text_.size()) {
                return Fail(L"unterminated string escape");
            }
            character = text_[position_++];
            switch (character) {
                case L'\"': value->push_back(L'\"'); break;
                case L'\\': value->push_back(L'\\'); break;
                case L'/': value->push_back(L'/'); break;
                case L'b': value->push_back(L'\b'); break;
                case L'f': value->push_back(L'\f'); break;
                case L'n': value->push_back(L'\n'); break;
                case L'r': value->push_back(L'\r'); break;
                case L't': value->push_back(L'\t'); break;
                case L'u': {
                    unsigned codeUnit = 0;
                    if (!ParseHexCodeUnit(&codeUnit)) return false;
                    if (codeUnit >= 0xD800 && codeUnit <= 0xDBFF) {
                        if (position_ + 2 > text_.size() ||
                            text_[position_] != L'\\' ||
                            text_[position_ + 1] != L'u') {
                            return Fail(L"missing low Unicode surrogate");
                        }
                        position_ += 2;
                        unsigned low = 0;
                        if (!ParseHexCodeUnit(&low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) {
                            return Fail(L"invalid low Unicode surrogate");
                        }
                        value->push_back(static_cast<wchar_t>(codeUnit));
                        value->push_back(static_cast<wchar_t>(low));
                    } else if (codeUnit >= 0xDC00 && codeUnit <= 0xDFFF) {
                        return Fail(L"unexpected low Unicode surrogate");
                    } else {
                        value->push_back(static_cast<wchar_t>(codeUnit));
                    }
                    break;
                }
                default:
                    return Fail(L"invalid string escape");
            }
        }
        return Fail(L"unterminated string");
    }

    bool ParseHexCodeUnit(unsigned* value) {
        if (position_ + 4 > text_.size()) {
            return Fail(L"incomplete Unicode escape");
        }
        unsigned result = 0;
        for (int index = 0; index < 4; ++index) {
            const wchar_t character = text_[position_++];
            unsigned digit = 0;
            if (character >= L'0' && character <= L'9') {
                digit = character - L'0';
            } else if (character >= L'a' && character <= L'f') {
                digit = character - L'a' + 10;
            } else if (character >= L'A' && character <= L'F') {
                digit = character - L'A' + 10;
            } else {
                return Fail(L"invalid Unicode escape");
            }
            result = result * 16 + digit;
        }
        *value = result;
        return true;
    }

    bool ParseNumber(std::wstring* value) {
        const size_t start = position_;
        if (text_[position_] == L'-') ++position_;
        if (position_ >= text_.size()) return Fail(L"incomplete number");
        if (text_[position_] == L'0') {
            ++position_;
            if (position_ < text_.size() && iswdigit(text_[position_])) {
                return Fail(L"leading zero in number");
            }
        } else if (text_[position_] >= L'1' && text_[position_] <= L'9') {
            while (position_ < text_.size() &&
                   iswdigit(text_[position_])) {
                ++position_;
            }
        } else {
            return Fail(L"invalid number");
        }
        if (position_ < text_.size() && text_[position_] == L'.') {
            ++position_;
            const size_t fraction = position_;
            while (position_ < text_.size() &&
                   iswdigit(text_[position_])) {
                ++position_;
            }
            if (position_ == fraction) {
                return Fail(L"missing fractional digits");
            }
        }
        if (position_ < text_.size() &&
            (text_[position_] == L'e' || text_[position_] == L'E')) {
            ++position_;
            if (position_ < text_.size() &&
                (text_[position_] == L'+' || text_[position_] == L'-')) {
                ++position_;
            }
            const size_t exponent = position_;
            while (position_ < text_.size() &&
                   iswdigit(text_[position_])) {
                ++position_;
            }
            if (position_ == exponent) {
                return Fail(L"missing exponent digits");
            }
        }
        *value = std::wstring(text_.substr(start, position_ - start));
        return true;
    }

    bool ConsumeLiteral(std::wstring_view literal) {
        if (position_ + literal.size() > text_.size() ||
            text_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    bool Fail(const std::wstring& message) {
        if (message_.empty()) {
            message_ = message;
            errorPosition_ = position_;
        }
        return false;
    }

    bool FinishError(std::wstring* error) const {
        if (!error) return false;
        size_t line = 1;
        size_t column = 1;
        for (size_t index = 0;
             index < std::min(errorPosition_, text_.size()); ++index) {
            if (text_[index] == L'\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        *error = L"line " + std::to_wstring(line) + L", column " +
                 std::to_wstring(column) + L": " + message_;
        return false;
    }

    std::wstring_view text_;
    std::unordered_map<std::wstring, std::wstring>* values_;
    std::unordered_map<std::wstring, JsonValueKind>* kinds_;
    size_t position_ = 0;
    size_t errorPosition_ = 0;
    std::wstring message_;
};

bool DecodeUtf8(const std::string& bytes, std::wstring* text,
                std::wstring* error) {
    size_t offset = 0;
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        offset = 3;
    }
    const size_t byteCount = bytes.size() - offset;
    if (byteCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
        if (error) *error = L"configuration is too large";
        return false;
    }
    if (byteCount == 0) {
        if (error) *error = L"configuration is empty";
        return false;
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + offset,
        static_cast<int>(byteCount), nullptr, 0);
    if (!required) {
        if (error) *error = L"configuration is not valid UTF-8";
        return false;
    }
    text->resize(required);
    return MultiByteToWideChar(
               CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + offset,
               static_cast<int>(byteCount), text->data(), required) == required;
}

}  // namespace

bool JsonConfig::LoadFile(const wchar_t* path, std::wstring* error) {
    values_.clear();
    kinds_.clear();
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = GetLastError() == ERROR_FILE_NOT_FOUND
                ? L"file not found"
                : L"unable to open file (error " +
                      std::to_wstring(GetLastError()) + L")";
        }
        return false;
    }

    LARGE_INTEGER size{};
    bool success = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
                   size.QuadPart <= kMaximumConfigBytes;
    std::string bytes;
    if (success) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                           &read, nullptr) && read == bytes.size();
    }
    CloseHandle(file);
    if (!success) {
        if (error) *error = L"file is empty, too large, or unreadable";
        return false;
    }

    std::wstring text;
    if (!DecodeUtf8(bytes, &text, error)) return false;
    return Parse(text, error);
}

bool JsonConfig::Parse(std::wstring_view text, std::wstring* error) {
    std::unordered_map<std::wstring, std::wstring> parsed;
    std::unordered_map<std::wstring, JsonValueKind> parsedKinds;
    Parser parser(text, &parsed, &parsedKinds);
    if (!parser.Run(error)) {
        values_.clear();
        kinds_.clear();
        return false;
    }
    values_ = std::move(parsed);
    kinds_ = std::move(parsedKinds);
    return true;
}

bool JsonConfig::Get(std::wstring_view path, std::wstring* value) const {
    if (!value) return false;
    const auto found = values_.find(std::wstring(path));
    if (found == values_.end()) return false;
    *value = found->second;
    return true;
}

std::vector<std::wstring> JsonConfig::Paths() const {
    std::vector<std::wstring> paths;
    paths.reserve(values_.size());
    for (const auto& [path, value] : values_) {
        (void)value;
        paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

std::vector<JsonEntry> JsonConfig::Entries() const {
    std::vector<JsonEntry> entries;
    entries.reserve(kinds_.size());
    for (const auto& [path, kind] : kinds_) entries.push_back({path, kind});
    std::sort(entries.begin(), entries.end(),
              [](const JsonEntry& first, const JsonEntry& second) {
                  return first.path < second.path;
              });
    return entries;
}

}  // namespace tas
