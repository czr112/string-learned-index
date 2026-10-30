#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>
#include <stdexcept>

namespace bm_native_runtime {
inline void emit_modules() {
    std::cout<<'{';bool first=true;
    for(const auto* name:{"libstdc++-6.dll","libgcc_s_seh-1.dll","libwinpthread-1.dll"}) {
        const auto module=GetModuleHandleA(name);if(!module)continue;
        char location[32768];const auto length=GetModuleFileNameA(module,location,sizeof(location));
        if(!length||length>=sizeof(location))throw std::runtime_error("runtime module path");
        if(!first)std::cout<<',';first=false;std::cout<<'"'<<name<<"\":\"";
        for(DWORD i=0;i<length;++i) {
            const char c=location[i];
            if(static_cast<unsigned char>(c)<32)throw std::runtime_error("runtime path control byte");
            if(c=='\\'||c=='"')std::cout<<'\\';std::cout<<c;
        }
        std::cout<<'"';
    }
    std::cout<<'}';
}
}
