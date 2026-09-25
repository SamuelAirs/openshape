// OpenShape shim for FreeCAD's Base::Console, used by PlaneGCS for
// diagnostics. Messages go to stderr only when OPENSHAPE_PLANEGCS_LOG is set.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

namespace Base {

class ConsoleShim {
public:
    template <typename... Args>
    void log(std::format_string<Args...> fmt, Args&&... args)
    {
        write(std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        write(std::format(fmt, std::forward<Args>(args)...));
    }
    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args)
    {
        write(std::format(fmt, std::forward<Args>(args)...));
    }

private:
    static void write(const std::string& text)
    {
        static const bool enabled = std::getenv("OPENSHAPE_PLANEGCS_LOG") != nullptr;
        if (enabled)
            std::fputs(text.c_str(), stderr);
    }
};

inline ConsoleShim& Console()
{
    static ConsoleShim console;
    return console;
}

} // namespace Base
