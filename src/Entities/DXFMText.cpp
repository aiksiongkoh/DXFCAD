#include "DXFMText.h"
#include "EntityCoordinates.h"
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/Drafting.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace DXFCAD::Detail {
namespace {

std::string unicodeEscape(const std::string& content,std::size_t& command)
{
    auto unit=[&](std::size_t at) {
        if (at>=content.size() || content.size()-at<6 || content.compare(at,2,"U+")!=0)
            throw std::invalid_argument("Incomplete MTEXT Unicode escape");
        unsigned value=0;
        for (std::size_t j=at+2;j<at+6;++j) {
            const char digit=content[j];
            unsigned hex;
            if (digit>='0' && digit<='9') hex=digit-'0';
            else if (digit>='A' && digit<='F') hex=digit-'A'+10;
            else if (digit>='a' && digit<='f') hex=digit-'a'+10;
            else throw std::invalid_argument("Invalid MTEXT Unicode escape");
            value=value*16+hex;
        }
        return value;
    };
    unsigned value=unit(command);
    command+=5;
    if (value>=0xd800 && value<=0xdbff) {
        if (command+1>=content.size() || content[command+1]!='\\')
            throw std::invalid_argument("MTEXT Unicode high surrogate lacks a low surrogate");
        const unsigned low=unit(command+2);
        if (low<0xdc00 || low>0xdfff) throw std::invalid_argument("Invalid MTEXT Unicode surrogate pair");
        value=0x10000+(value-0xd800)*0x400+(low-0xdc00);
        command+=7;
    } else if (value>=0xdc00 && value<=0xdfff) {
        throw std::invalid_argument("Unpaired MTEXT Unicode low surrogate");
    }
    if (value<32 || value==127) throw std::invalid_argument("MTEXT Unicode control characters are not supported yet");
    std::string result;
    if (value<0x80) result+=static_cast<char>(value);
    else {
        if (value<0x800) result+=static_cast<char>(0xc0|(value>>6));
        else {
            if (value<0x10000) result+=static_cast<char>(0xe0|(value>>12));
            else {
                result+=static_cast<char>(0xf0|(value>>18));
                result+=static_cast<char>(0x80|((value>>12)&63));
            }
            result+=static_cast<char>(0x80|((value>>6)&63));
        }
        result+=static_cast<char>(0x80|(value&63));
    }
    return result;
}

void validateSubstituteFont(const std::string& declaration)
{
    const auto firstOption = declaration.find('|');
    if (declaration.substr(0, firstOption).empty())
        throw std::invalid_argument("Empty MTEXT font name");
    if (declaration.find_first_of("\\{}\t\r\n") != std::string::npos)
        throw std::invalid_argument("Invalid MTEXT font declaration");
    for (auto start = firstOption; start != std::string::npos;) {
        const auto end = declaration.find('|', start+1);
        const auto option = declaration.substr(start+1, end == std::string::npos ? end : end-start-1);
        if (option != "b0" && option != "i0") {
            if (option.size() < 2 || (option[0] != 'c' && option[0] != 'p') ||
                !std::all_of(option.begin()+1, option.end(), [](char c) { return c >= '0' && c <= '9'; }))
                throw std::invalid_argument("MTEXT bold, italic, or unknown font options are not supported yet");
        }
        start = end;
    }
    // Like the entity's text style, the declared face uses the user-selected
    // substitute font. No font file is loaded by the domain object.
}

std::pair<std::string, double> uniformText(const std::string& content, double nominalHeight)
{
    std::string plain;
    double currentHeight = nominalHeight;
    std::optional<double> visibleHeight;
    std::vector<double> groups;
    bool mixedSpace = false;
    auto append = [&](const std::string& value) {
        if (value.find_first_not_of(" \n") != std::string::npos) {
            if (mixedSpace || (visibleHeight && std::abs(*visibleHeight-currentHeight) >
                    1e-12*std::max(*visibleHeight, currentHeight)))
                throw std::invalid_argument("MTEXT mixed character heights are not supported yet");
            visibleHeight = currentHeight;
        } else if (value == "\n") {
            mixedSpace = false;
        } else if (visibleHeight && std::abs(*visibleHeight-currentHeight) >
                1e-12*std::max(*visibleHeight, currentHeight)) {
            // Leading/trailing spaces have no outline bounds. Interior spaces
            // must keep the uniform height too, since their advance matters.
            mixedSpace = true;
        }
        plain += value;
    };
    for (std::size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];
        if (c == '\\') {
            if (++i == content.size()) throw std::invalid_argument("Incomplete MTEXT escape");
            const char command = content[i];
            if (command == 'P') append("\n");
            else if (command == '\\' || command == '{' || command == '}') append(std::string(1, command));
            else if (command == '~') append("\xc2\xa0");
            else if (command == 'U') append(unicodeEscape(content,i));
            else if (command == 'H' || command == 'f' || command == 'F') {
                const auto end = content.find(';', i+1);
                if (end == std::string::npos) throw std::invalid_argument("Incomplete MTEXT formatting command");
                auto argument = content.substr(i+1, end-i-1);
                if (command == 'H') {
                    const bool relative = !argument.empty() && argument.back() == 'x';
                    if (relative) argument.pop_back();
                    currentHeight = numeric(argument)*(relative ? nominalHeight : 1);
                    if (!std::isfinite(currentHeight) || currentHeight <= 0)
                        throw std::invalid_argument("Invalid MTEXT character height");
                } else validateSubstituteFont(argument);
                i = end;
            } else throw std::invalid_argument("MTEXT inline formatting is not supported yet");
        } else if (c == '{') {
            if (groups.size() == 8) throw std::invalid_argument("MTEXT groups exceed eight nesting levels");
            groups.push_back(currentHeight);
        } else if (c == '}') {
            if (groups.empty()) throw std::invalid_argument("Unmatched MTEXT closing brace");
            currentHeight = groups.back();
            groups.pop_back();
        } else if (static_cast<unsigned char>(c) < 32) {
            throw std::invalid_argument("MTEXT tabs or control characters are not supported yet");
        } else append(std::string(1, c));
    }
    if (!groups.empty()) throw std::invalid_argument("Unclosed MTEXT group");
    return {plain, visibleHeight.value_or(nominalHeight)};
}

} // namespace

std::string DXFMText::translated(const Vector3D& offset) const { return translatedPoints(offset,{10},false); }

EntityGeometry DXFMText::geometry() const {
    const auto fields = nativeEntityFields(record_);
    const int attachment = fields.integer(71, 1), direction = fields.integer(72, 1);
    if (attachment < 1 || attachment > 9 || (direction != 1 && direction != 5))
        throw std::invalid_argument("Unsupported MTEXT attachment or drawing direction");
    const double height = fields.number(40), width = fields.number(41), spacing = fields.number(44, 1);
    const int spacingStyle = fields.integer(73, 1);
    if (height <= 0 || width < 0 || spacing < 0.25 || spacing > 4 || (spacingStyle != 1 && spacingStyle != 2))
        throw std::invalid_argument("Invalid MTEXT height, width, or line spacing");
    if (fields.integer(75) || fields.integer(76) > 1 || fields.integer(90))
        throw std::invalid_argument("MTEXT columns and background fills are not supported yet");
    std::string content;
    for (const auto& [code, value] : fields.groups) if (code == 3 || code == 1) content += value;
    const auto [plain, displayHeight] = uniformText(content, height);
    const CartesianFrame frame(extrusion(fields));
    const auto normal = frame.normal();
    Vector3D x = frame.xAxis();
    // A direction vector and rotation can coexist; the last one wins.
    bool useVector = false;
    double angle = 0;
    for (const auto& [code, value] : fields.groups) {
        if (code == 11) useVector = true;
        if (code == 50) { useVector = false; angle = numeric(value); }
    }
    if (useVector) {
        x = point(fields, 11);
        const double length = std::hypot(x.x, x.y, x.z);
        if (!std::isfinite(length) || length == 0) throw std::invalid_argument("Invalid MTEXT direction vector");
        x = {x.x/length, x.y/length, x.z/length};
        if (std::abs(x.x*normal.x+x.y*normal.y+x.z*normal.z) > 1e-7)
            throw std::invalid_argument("MTEXT direction must lie in its extrusion plane");
    } else {
        const auto a = frame.xAxis(), b = frame.yAxis();
        x = {std::cos(angle)*a.x+std::sin(angle)*b.x, std::cos(angle)*a.y+std::sin(angle)*b.y,
             std::cos(angle)*a.z+std::sin(angle)*b.z};
    }
    Vector3D y{normal.y*x.z-normal.z*x.y, normal.z*x.x-normal.x*x.z, normal.x*x.y-normal.y*x.x};
    // MTEXT insertion/direction are WCS, unlike TEXT's OCS insertion.
    TextGeometry text{plain, point(fields, 10), x, y, normal, displayHeight, 1};
    text.multiline = true;
    text.referenceWidth = width;
    // Exact spacing follows the nominal height. At-least spacing also admits
    // taller characters; an inline height override does not change group 40.
    text.lineSpacing = spacing*(spacingStyle == 2 ? height : std::max(height, displayHeight))/displayHeight;
    if (!std::isfinite(text.lineSpacing) || text.lineSpacing <= 0)
        throw std::invalid_argument("Invalid MTEXT display line spacing");
    text.attachment = attachment;
    return {{}, {}, text};
}

} // namespace DXFCAD::Detail
