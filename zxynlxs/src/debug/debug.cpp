#include "debug.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>

namespace debug
{
    void Initialize()
    {
    }

    void Log(const char* format, ...)
    {
#ifdef CS2_INTERNAL_DEBUG
        if (!format)
            return;

        char message[1024]{};
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(message, sizeof(message), format, arguments);
        va_end(arguments);

        OutputDebugStringA(message);
        OutputDebugStringA("\n");
#else
        (void)format;
#endif
    }
}
