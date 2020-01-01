#include "pch.h"

#include "logging.h"

void DebugPrint(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    size_t len = _vscprintf(fmt, args) + 1;
    std::vector<char> buff(len);
    vsnprintf_s(buff.data(), len, _TRUNCATE, fmt, args);
    OutputDebugStringA(buff.data());
    va_end(args);
}
