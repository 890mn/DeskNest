// test/stubs/Arduino.h
// 栖屏 DeskNest — native smoke-test 用最小 Arduino 桩
// "栖于桌面，息于常亮之间"
//
// 只覆盖 gesture.cpp / gesture.h 编译与运行所需的子集：
//   - millis()                → g_mock_millis（外部控制时钟）
//   - Serial（Print 派生）    → stdout（printf 风格 + 流式 println）
//   - String                  → std::string 别名（当前测试用不到，仅占位）
//
// 加 platformio.ini 的 -I test/stubs 后，<Arduino.h> 优先解析到这里，
// 不去碰 framework-arduino* / unihiker_k10.h（K10 BSP），从而把 host 测试
// 和真机烧录彻底解耦。

#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

// 受测代码可以显式访问/修改（在 test_*.cpp 里实现）。
extern uint32_t g_mock_millis;

inline uint32_t millis() { return g_mock_millis; }

// ---------------------------------------------------------------------------
// Print：覆盖 gesture.cpp 用到的 print/println/printf 重载
// ---------------------------------------------------------------------------

class Print {
public:
    void print(const char* s) { std::fputs(s ? s : "(null)", stdout); }
    void print(int v)         { std::fprintf(stdout, "%d", v); }
    void print(unsigned v)    { std::fprintf(stdout, "%u", v); }
    void print(long v)        { std::fprintf(stdout, "%ld", v); }
    void print(unsigned long v){ std::fprintf(stdout, "%lu", v); }
    void print(double v, int prec = 2)  { std::fprintf(stdout, "%.*f", prec, v); }
    void print(float v,  int prec = 2)  { std::fprintf(stdout, "%.*f", prec, (double)v); }

    void println()            { std::fputc('\n', stdout); }
    void println(const char* s){ std::fputs(s ? s : "(null)", stdout); std::fputc('\n', stdout); }
    void println(int v)        { std::fprintf(stdout, "%d\n", v); }
    void println(unsigned v)   { std::fprintf(stdout, "%u\n", v); }
    void println(unsigned long v){ std::fprintf(stdout, "%lu\n", v); }
    void println(double v, int prec = 2){ std::fprintf(stdout, "%.*f\n", prec, v); }
    void println(float v,  int prec = 2){ std::fprintf(stdout, "%.*f\n", prec, (double)v); }

    int printf(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = std::vfprintf(stdout, fmt, ap);
        va_end(ap);
        return n;
    }
};

class SerialClass : public Print {
public:
    void begin(unsigned long = 0) {}
    int  available() { return 0; }   // test stub: 没数据，避免阻塞
    int  read()      { return -1; }
};

extern SerialClass Serial;

// ---------------------------------------------------------------------------
// 占位：sensors.h 通过 <Arduino.h> 引入，但本测试不调 sensors.cpp，
// 这里给一个最简别名，避免任何潜在冲突。
// ---------------------------------------------------------------------------

typedef std::string String;