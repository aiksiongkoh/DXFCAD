#pragma once

#include <DXFCAD/GroupSequence.h>
#include <algorithm>

namespace DXFCAD::Detail {

inline bool usesWindows1252(const GroupSequence& drawing)
{
    std::string version, codepage;
    bool header = false;
    for (std::size_t i = 0; i + 1 < drawing.groups.size(); ++i) {
        const auto& group = drawing.groups[i];
        if (group == std::make_pair(0, std::string("SECTION")))
            header = drawing.groups[i + 1] == std::make_pair(2, std::string("HEADER"));
        if (header && group == std::make_pair(0, std::string("ENDSEC"))) break;
        if (!header || group.first != 9) continue;
        if (group.second == "$ACADVER") version = drawing.groups[i + 1].second;
        if (group.second == "$DWGCODEPAGE") codepage = drawing.groups[i + 1].second;
    }
    return codepage == "ANSI_1252" && (version.empty() || version < "AC1021");
}

inline unsigned windows1252Codepoint(unsigned char byte)
{
    constexpr unsigned special[] = {0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
        0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
        0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    return byte >= 0x80 && byte < 0xa0 ? special[byte - 0x80] : byte;
}

inline std::vector<std::size_t> textContentIndices(const GroupSequence& record)
{
    std::vector<std::size_t> indices;
    if (record.value(0) != "MTEXT" && record.value(0) != "TEXT") return indices;
    int depth = 0;
    for (std::size_t i = 0; i < record.groups.size(); ++i) {
        const auto& [code, value] = record.groups[i];
        if (code == 1001 && !depth) break;
        if (code == 102) {
            if (!value.empty() && value.front() == '{') ++depth;
            else if (value == "}" && depth) --depth;
        } else if (!depth && (code == 1 || (code == 3 && record.value(0) == "MTEXT"))) indices.push_back(i);
    }
    return indices;
}

// FreeCAD properties carry Unicode, whereas native records carry bytes. Use
// native DXF Unicode escapes only for the adapter's legacy TEXT/MTEXT content.
inline GroupSequence escapedLegacyText(const GroupSequence& record)
{
    const auto indices = textContentIndices(record);
    if (indices.empty()) return record;
    GroupSequence result;
    std::size_t next = 0;
    constexpr char hex[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < record.groups.size(); ++i) {
        const auto& group = record.groups[i];
        if (next == indices.size() || indices[next] != i) {
            result.groups.push_back(group);
            continue;
        }
        ++next;
        std::string text;
        for (unsigned char byte : group.second) {
            if (byte < 0x80) text += static_cast<char>(byte);
            else {
                const auto codepoint = windows1252Codepoint(byte);
                text += "\\U+";
                for (int shift : {12, 8, 4, 0}) text += hex[(codepoint >> shift) & 15];
            }
        }
        // MTEXT joins continuation chunks before interpreting escapes.
        std::size_t start = 0;
        while (record.value(0) == "MTEXT" && text.size() - start > 250) {
            result.groups.emplace_back(3, text.substr(start, 250));
            start += 250;
        }
        result.groups.emplace_back(group.first, text.substr(start));
    }
    return result;
}

// Recover source bytes when content is unchanged, including after a geometry
// or property edit. Edited content retains its explicit native representation.
inline GroupSequence restoreLegacyText(const GroupSequence& original, const GroupSequence& replacement)
{
    const auto oldIndices = textContentIndices(original);
    const auto newIndices = textContentIndices(replacement);
    if (oldIndices.empty() || newIndices.empty()) return replacement;
    const auto escaped = escapedLegacyText(original);
    const auto escapedIndices = textContentIndices(escaped);
    std::string expected, actual;
    for (auto i : escapedIndices) expected += escaped.groups[i].second;
    for (auto i : newIndices) actual += replacement.groups[i].second;
    if (expected != actual) return replacement;
    if (escapedIndices.size() == newIndices.size() &&
        std::equal(escapedIndices.begin(), escapedIndices.end(), newIndices.begin(),
            [&](auto a, auto b) { return escaped.groups[a] == replacement.groups[b]; })) {
        // Restore chunks in place even if a transform inserted optional
        // coordinates, retaining intervening metadata and opaque groups.
        std::vector<std::size_t> origins;
        for (auto i : oldIndices) {
            const auto& value = original.groups[i].second;
            const auto high = std::count_if(value.begin(), value.end(),
                [](unsigned char byte) { return byte >= 0x80; });
            const auto length = value.size() + 6 * high;
            const auto count = original.value(0) == "MTEXT" && length ? 1 + (length - 1) / 250 : 1;
            origins.push_back(i);
            for (std::size_t j = 1; j < count; ++j) origins.push_back(original.groups.size());
        }
        GroupSequence result;
        std::size_t next = 0;
        for (std::size_t i = 0; i < replacement.groups.size(); ++i) {
            if (next < newIndices.size() && newIndices[next] == i) {
                if (origins[next] < original.groups.size()) result.groups.push_back(original.groups[origins[next]]);
                ++next;
            } else result.groups.push_back(replacement.groups[i]);
        }
        return result;
    }
    GroupSequence result;
    std::size_t next = 0;
    for (std::size_t i = 0; i < replacement.groups.size(); ++i) {
        if (next < newIndices.size() && newIndices[next] == i) {
            if (next == 0)
                for (auto old : oldIndices) result.groups.push_back(original.groups[old]);
            ++next;
        } else result.groups.push_back(replacement.groups[i]);
    }
    return result;
}

} // namespace DXFCAD::Detail
