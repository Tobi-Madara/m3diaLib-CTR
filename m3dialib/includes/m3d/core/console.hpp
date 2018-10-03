#ifndef CONSOLE_H
#define CONSOLE_H

#pragma once

#include <3ds.h>
#include <string>
#include "m3d/graphics/renderContext.hpp"

namespace m3d {
    class Console {
        public:
            // gotta use an old-school enum so that one can use `m3d::Console::Red`
            enum ConsoleCode {
                BlackFG = 0,
                RedFG,
                GreenFG,
                YellowFG,
                BlueFG,
                MagentaFG,
                CyanFG,
                WhiteFG,

                BlackBG,
                RedBG,
                GreenBG,
                YellowBG,
                BlueBG,
                MagentaBG,
                CyanBG,
                WhiteBG,

                Bold,
                Faint,
                Underline,
                Strikethrough,

                Reset,
                Endl
            };

            static inline const std::string position(int t_x, int t_y) {
                return "\x1b[" + std::to_string(t_y) + ";" + std::to_string(t_x) + "H";
            }

            static void print(const std::string& t_data);
            static void print(const std::string& t_data, m3d::Console::ConsoleCode t_color);

            static void println(const std::string& t_data);
            static void println(const std::string& t_data, m3d::Console::ConsoleCode t_color);

            static void printAt(const std::string& t_data);
            static void printAt(const std::string& t_data, m3d::Console::ConsoleCode t_color);

            static void enableConsole(m3d::RenderContext::ScreenTarget t_target);

            m3d::Console& operator<<(const std::string& t_string);
            m3d::Console& operator<<(m3d::Console::ConsoleCode t_char);

        private:
            static const char* m_codeLUT[];
    };
}

#endif
