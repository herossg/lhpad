#include "core.h"
#include <unordered_map>
namespace lp {
// Preserve 0.5.2's bounded LCS / unique-anchor comparison, including aligned changed blocks.
std::vector<DiffRow> diffLines(const std::vector<std::string> &left, const std::vector<std::string> &right) {
    std::vector<DiffRow> rows;
    std::function<void(int, int, int, int, int)> region;
    region = [&](int a, int aEnd, int b, int bEnd, int depth) {
        while (a < aEnd && b < bEnd && left[a] == right[b])
            rows.push_back({a++, b++, 0});
        int suffix = 0;
        while (a < aEnd - suffix && b < bEnd - suffix && left[aEnd - 1 - suffix] == right[bEnd - 1 - suffix])
            suffix++;
        int ae = aEnd - suffix, be = bEnd - suffix;
        if (a == ae) {
            while (b < be)
                rows.push_back({-1, b++, 2});
        } else if (b == be) {
            while (a < ae)
                rows.push_back({a++, -1, 1});
        } else if (static_cast<int64_t>(ae - a) * (be - b) <= 1000000) {
            int n = ae - a, m = be - b;
            std::vector<int> table(static_cast<size_t>(n + 1) * (m + 1));
            auto at = [&](int x, int y) -> int & { return table[static_cast<size_t>(x) * (m + 1) + y]; };
            for (int x = n - 1; x >= 0; x--)
                for (int y = m - 1; y >= 0; y--)
                    at(x, y) = left[a + x] == right[b + y] ? at(x + 1, y + 1) + 1
                                                           : std::max(at(x + 1, y), at(x, y + 1));
            int x = 0, y = 0;
            while (x < n || y < m) {
                if (x < n && y < m && left[a + x] == right[b + y]) {
                    rows.push_back({a + x, b + y, 0});
                    x++;
                    y++;
                } else if (y >= m || x < n && at(x + 1, y) >= at(x, y + 1))
                    rows.push_back({a + x++, -1, 1});
                else
                    rows.push_back({-1, b + y++, 2});
            }
        } else {
            std::unordered_map<std::string_view, int> la, rb;
            for (int i = a; i < ae; i++) {
                auto [entry, inserted] = la.emplace(left[i], i);
                if (!inserted)
                    entry->second = -1;
            }
            for (int i = b; i < be; i++) {
                auto [entry, inserted] = rb.emplace(right[i], i);
                if (!inserted)
                    entry->second = -1;
            }
            std::vector<std::pair<int, int>> pairs;
            for (auto &[key, index] : la) {
                auto it = rb.find(key);
                if (index >= 0 && it != rb.end() && it->second >= 0)
                    pairs.emplace_back(index, it->second);
            }
            std::sort(pairs.begin(), pairs.end());
            std::vector<int> tails, previous(pairs.size());
            for (int i = 0; i < static_cast<int>(pairs.size()); i++) {
                int lo = 0, hi = static_cast<int>(tails.size());
                while (lo < hi) {
                    int mid = (lo + hi) / 2;
                    if (pairs[tails[mid]].second < pairs[i].second)
                        lo = mid + 1;
                    else
                        hi = mid;
                }
                previous[i] = lo ? tails[lo - 1] : -1;
                if (lo == static_cast<int>(tails.size()))
                    tails.push_back(i);
                else
                    tails[lo] = i;
            }
            if (tails.empty() || depth > 32) {
                while (a < ae || b < be)
                    rows.push_back({a < ae ? a++ : -1, b < be ? b++ : -1, 3});
            } else {
                std::vector<std::pair<int, int>> anchors;
                for (int i = tails.back(); i >= 0; i = previous[i])
                    anchors.push_back(pairs[i]);
                std::reverse(anchors.begin(), anchors.end());
                for (auto [x, y] : anchors) {
                    region(a, x, b, y, depth + 1);
                    rows.push_back({x, y, 0});
                    a = x + 1;
                    b = y + 1;
                }
                region(a, ae, b, be, depth + 1);
            }
        }
        for (int i = 0; i < suffix; i++)
            rows.push_back({ae + i, be + i, 0});
    };
    region(0, static_cast<int>(left.size()), 0, static_cast<int>(right.size()), 0);
    std::vector<DiffRow> aligned;
    for (size_t i = 0; i < rows.size();) {
        if (!rows[i].kind) {
            aligned.push_back(rows[i++]);
            continue;
        }
        std::vector<int64_t> a, b;
        while (i < rows.size() && rows[i].kind) {
            if (rows[i].left >= 0)
                a.push_back(rows[i].left);
            if (rows[i].right >= 0)
                b.push_back(rows[i].right);
            i++;
        }
        for (size_t j = 0; j < std::max(a.size(), b.size()); j++)
            aligned.push_back({j < a.size() ? a[j] : -1, j < b.size() ? b[j] : -1,
                               j < a.size() && j < b.size() ? 3
                               : j < a.size()               ? 1
                                                            : 2});
    }
    return aligned;
}
std::string compareKey(const std::string &line, bool ignoreSpace, bool ignoreCase) {
    std::string key;
    key.reserve(line.size());
    for (char c : line) {
        if (ignoreSpace && (c == ' ' || c == '\t'))
            continue;
        key += ignoreCase && c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return key;
}
namespace {
struct CodePoints {
    std::vector<uint32_t> value;
    std::vector<size_t> start; // Byte offset of each code point, plus the total length at the end.
};
CodePoints split(const std::string &s) {
    CodePoints out;
    for (size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        n = std::min(n, s.size() - i);
        uint32_t v = n == 1 ? c : c & (0x7F >> n);
        for (size_t k = 1; k < n; k++)
            v = v << 6 | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.value.push_back(v);
        out.start.push_back(i);
        i += n;
    }
    out.start.push_back(s.size());
    return out;
}
std::vector<std::pair<size_t, size_t>> ranges(const CodePoints &cp, const std::vector<bool> &changed) {
    std::vector<std::pair<size_t, size_t>> out;
    for (size_t i = 0; i < changed.size();) {
        if (!changed[i]) {
            i++;
            continue;
        }
        size_t j = i;
        while (j < changed.size() && changed[j])
            j++;
        out.emplace_back(cp.start[i], cp.start[j]);
        i = j;
    }
    return out;
}
} // namespace
InlineDiff inlineDiff(const std::string &left, const std::string &right) {
    auto a = split(left), b = split(right);
    size_t n = a.value.size(), m = b.value.size(), prefix = 0, suffix = 0;
    while (prefix < n && prefix < m && a.value[prefix] == b.value[prefix])
        prefix++;
    while (suffix < n - prefix && suffix < m - prefix && a.value[n - 1 - suffix] == b.value[m - 1 - suffix])
        suffix++;
    std::vector<bool> changedA(n, false), changedB(m, false);
    size_t x0 = prefix, x1 = n - suffix, y0 = prefix, y1 = m - suffix;
    size_t w = x1 - x0, h = y1 - y0;
    if (w && h && w * h <= 250000) {
        // Character LCS on the differing middle part, so "a1b2" vs "a3b4" marks only the digits.
        std::vector<uint16_t> table((w + 1) * (h + 1), 0);
        auto at = [&](size_t x, size_t y) -> uint16_t & { return table[x * (h + 1) + y]; };
        for (size_t x = w; x-- > 0;)
            for (size_t y = h; y-- > 0;)
                at(x, y) = a.value[x0 + x] == b.value[y0 + y]
                               ? static_cast<uint16_t>(at(x + 1, y + 1) + 1)
                               : std::max(at(x + 1, y), at(x, y + 1));
        size_t x = 0, y = 0;
        while (x < w || y < h) {
            if (x < w && y < h && a.value[x0 + x] == b.value[y0 + y]) {
                x++;
                y++;
            } else if (y >= h || (x < w && at(x + 1, y) >= at(x, y + 1)))
                changedA[x0 + x++] = true;
            else
                changedB[y0 + y++] = true;
        }
    } else {
        for (size_t i = x0; i < x1; i++)
            changedA[i] = true;
        for (size_t i = y0; i < y1; i++)
            changedB[i] = true;
    }
    return {ranges(a, changedA), ranges(b, changedB)};
}
} // namespace lp
