#pragma once
#include <windows.h>
#include <string>

namespace lp {
// Independent from LHZip's COM identity.
inline constexpr GUID explorerCommandId{0xd77fb6f2, 0x1c04, 0x4e31,
                                        {0x98, 0x68, 0x9c, 0xb1, 0x39, 0xf3, 0xd2, 0xc8}};
inline std::wstring quoteShellArgument(const std::wstring &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
}
