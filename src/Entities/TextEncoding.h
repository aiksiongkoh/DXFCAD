#pragma once

#include <stdexcept>
#include <string>

namespace DXFCAD::Detail {

// command points at 'U' on entry and the last consumed hex digit on return.
inline std::string unicodeEscape(const std::string& content,std::size_t& command,const std::string& type = "MTEXT")
{
    auto unit=[&](std::size_t at) {
        if (at>=content.size() || content.size()-at<6 || content.compare(at,2,"U+")!=0)
            throw std::invalid_argument(type+" Unicode escape is incomplete");
        unsigned value=0;
        for (std::size_t j=at+2;j<at+6;++j) {
            const char digit=content[j];
            unsigned hex;
            if (digit>='0' && digit<='9') hex=digit-'0';
            else if (digit>='A' && digit<='F') hex=digit-'A'+10;
            else if (digit>='a' && digit<='f') hex=digit-'a'+10;
            else throw std::invalid_argument(type+" Unicode escape is invalid");
            value=value*16+hex;
        }
        return value;
    };
    unsigned value=unit(command);
    command+=5;
    if (value>=0xd800 && value<=0xdbff) {
        if (command+1>=content.size() || content[command+1]!='\\')
            throw std::invalid_argument(type+" Unicode high surrogate lacks a low surrogate");
        const unsigned low=unit(command+2);
        if (low<0xdc00 || low>0xdfff) throw std::invalid_argument(type+" Unicode surrogate pair is invalid");
        value=0x10000+(value-0xd800)*0x400+(low-0xdc00);
        command+=7;
    } else if (value>=0xdc00 && value<=0xdfff) {
        throw std::invalid_argument(type+" Unicode low surrogate is unpaired");
    }
    if (value<32 || value==127) throw std::invalid_argument(type+" Unicode control characters are not supported yet");
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

} // namespace DXFCAD::Detail
