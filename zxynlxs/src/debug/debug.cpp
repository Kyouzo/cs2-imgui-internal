#include "debug.h"

#include <Windows.h>
#include <iostream>
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace debug
{
    static std::mutex log_mutex;

    void Initialize()
    {
        if (!GetConsoleWindow())
        {
            if (!AllocConsole())
                return;
        }

        FILE* fp = nullptr;

        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);

        SetConsoleTitleA("CS2 Debug Console");

        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();

        std::cout << "[console] initialized successfully\n";
    }

    void Log(const char* format, ...)
    {
        if (!format)
            return;

        char message[1024]{};

        va_list arguments;
        va_start(arguments, format);
        vsnprintf(message, sizeof(message), format, arguments);
        va_end(arguments);

        std::lock_guard<std::mutex> lock(log_mutex);

        printf("%s\n", message);
        fflush(stdout);
    }
}