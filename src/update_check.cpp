#include "update_check.h"

#include <cctype>
#include <vector>

namespace update {
namespace {

// Appends a code point as UTF-8.
void appendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// Reads the JSON string starting at the opening quote `json[at]`. Returns the
// index just past the closing quote, or npos when the string is malformed.
size_t readString(const std::string& json, size_t at, std::string* value) {
    value->clear();
    for (size_t i = at + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') return i + 1;
        if (c != '\\') {
            value->push_back(c);
            continue;
        }
        if (++i >= json.size()) break;
        switch (json[i]) {
            case 'n': value->push_back('\n'); break;
            case 't': value->push_back('\t'); break;
            case 'r': value->push_back('\r'); break;
            case 'b': value->push_back('\b'); break;
            case 'f': value->push_back('\f'); break;
            case 'u': {
                if (i + 4 >= json.size()) return std::string::npos;
                unsigned code = 0;
                for (size_t k = i + 1; k <= i + 4; ++k) {
                    int digit = std::isxdigit(static_cast<unsigned char>(json[k]))
                                    ? (std::isdigit(static_cast<unsigned char>(json[k]))
                                           ? json[k] - '0'
                                           : (std::tolower(static_cast<unsigned char>(json[k])) - 'a' + 10))
                                    : -1;
                    if (digit < 0) return std::string::npos;
                    code = code * 16 + static_cast<unsigned>(digit);
                }
                appendUtf8(*value, code);
                i += 4;
                break;
            }
            default: value->push_back(json[i]); break; // \" \\ \/
        }
    }
    return std::string::npos;
}

// Every string value of `"key": "..."` in document order. A flat scan is
// enough here: the keys we want carry unique, string-valued meanings.
std::vector<std::string> stringValues(const std::string& json, const std::string& key) {
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\"";
    size_t at = 0;
    while ((at = json.find(needle, at)) != std::string::npos) {
        size_t i = at + needle.size();
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        if (i < json.size() && json[i] == ':') {
            ++i;
            while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
            if (i < json.size() && json[i] == '"') {
                std::string value;
                size_t end = readString(json, i, &value);
                if (end == std::string::npos) break;
                values.push_back(value);
                at = end;
                continue;
            }
        }
        at += needle.size();
    }
    return values;
}

bool endsWithNoCase(const std::string& text, const std::string& suffix) {
    if (suffix.size() > text.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[text.size() - suffix.size() + i])));
        char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

std::vector<long> versionParts(const std::string& version) {
    std::vector<long> parts;
    size_t i = 0;
    if (i < version.size() && (version[i] == 'v' || version[i] == 'V')) ++i;
    while (i < version.size() && std::isdigit(static_cast<unsigned char>(version[i]))) {
        long part = 0;
        while (i < version.size() && std::isdigit(static_cast<unsigned char>(version[i]))) {
            part = part * 10 + (version[i] - '0');
            ++i;
        }
        parts.push_back(part);
        if (i < version.size() && version[i] == '.') ++i;
        else break;
    }
    return parts;
}

} // namespace

bool parseLatestRelease(const std::string& json, ReleaseInfo* out) {
    *out = ReleaseInfo();
    std::vector<std::string> tags = stringValues(json, "tag_name");
    if (tags.empty() || tags[0].empty()) return false;
    out->tag = tags[0];

    // The release's own html_url comes before those of its author and assets.
    std::vector<std::string> pages = stringValues(json, "html_url");
    if (!pages.empty()) out->pageUrl = pages[0];

    // Prefer an asset named Markr.exe; fall back to any .exe.
    for (const std::string& url : stringValues(json, "browser_download_url")) {
        if (endsWithNoCase(url, "/Markr.exe")) {
            out->assetUrl = url;
            break;
        }
        if (out->assetUrl.empty() && endsWithNoCase(url, ".exe")) out->assetUrl = url;
    }
    return true;
}

int compareVersions(const std::string& a, const std::string& b) {
    std::vector<long> x = versionParts(a);
    std::vector<long> y = versionParts(b);
    size_t count = x.size() > y.size() ? x.size() : y.size();
    for (size_t i = 0; i < count; ++i) {
        long p = i < x.size() ? x[i] : 0;
        long q = i < y.size() ? y[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

} // namespace update
