#pragma once

void DebugPrint(const char* fmt, ...);

#define BASSERT(expr) ((void)((!!(expr)) || \
        ((DebugPrint("Error: %s, %s:%d\n", #expr, __FILE__, __LINE__),1) \
         && (__debugbreak(),0)) ))


