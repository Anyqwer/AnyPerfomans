#pragma once

#include <string>
#include <windows.h>

namespace anyperf {

inline std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

inline std::string escape_html(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '&':  result += "&amp;";  break;
            case '<':  result += "&lt;";   break;
            case '>':  result += "&gt;";   break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;";  break;
            default:   result += c;         break;
        }
    }
    return result;
}

} // namespace anyperf
