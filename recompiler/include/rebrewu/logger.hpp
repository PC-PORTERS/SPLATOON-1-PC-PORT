#pragma once

// Colored console logger shared by RebrewU and the native port.
//
// A log line is a level, an optional tag and a printf-style message:
//
//     [INFO] [gx2] plaza 60 FPS mode sync installed
//
// Colors are enabled only when the destination is a terminal (or when
// RBREW_COLOR forces them) and can be turned off with NO_COLOR.
// RBREW_LOG_LEVEL selects the minimum level (debug, info, warning, error,
// fatal, off); the default keeps every message, like the old printf
// diagnostics did.
//
// Builds with RBREW_DISABLE_LOGGING compile every call out, including its
// arguments.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rbrew::log {

enum class Level : int { Debug = 0, Info, Warning, Error, Fatal, Off };

namespace detail {

inline FILE* stream_for(Level) {
    return stderr;
}

inline bool is_terminal(FILE* stream) {
#if defined(_WIN32)
    return _isatty(_fileno(stream)) != 0;
#else
    return ::isatty(::fileno(stream)) != 0;
#endif
}

inline void enable_virtual_terminal() {
#if defined(_WIN32)
    static std::once_flag once;
    std::call_once(once, [] {
        const DWORD handles[] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
        for (DWORD handle_id : handles) {
            HANDLE handle = GetStdHandle(handle_id);
            DWORD mode = 0;
            if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode))
                SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    });
#endif
}

inline bool detect_color() {
    if (const char* no_color = std::getenv("NO_COLOR"); no_color && *no_color) return false;
    if (const char* forced = std::getenv("RBREW_COLOR"); forced && *forced) return std::strcmp(forced, "0") != 0;
    if (const char* term = std::getenv("TERM"); term && std::strcmp(term, "dumb") == 0) return false;
    enable_virtual_terminal();
    return is_terminal(stderr);
}

inline bool& color_enabled() {
    static bool enabled = detect_color();
    return enabled;
}

inline Level parse_level(const char* name) {
    if (!name || !*name) return Level::Debug;
    if (std::strcmp(name, "debug") == 0) return Level::Debug;
    if (std::strcmp(name, "info") == 0) return Level::Info;
    if (std::strcmp(name, "warning") == 0 || std::strcmp(name, "warn") == 0) return Level::Warning;
    if (std::strcmp(name, "error") == 0) return Level::Error;
    if (std::strcmp(name, "fatal") == 0) return Level::Fatal;
    if (std::strcmp(name, "off") == 0 || std::strcmp(name, "none") == 0) return Level::Off;
    return Level::Debug;
}

inline Level& minimum_level() {
    static Level level = parse_level(std::getenv("RBREW_LOG_LEVEL"));
    return level;
}

inline std::mutex& output_mutex() {
    static std::mutex mutex;
    return mutex;
}

inline const char* level_name(Level level) {
    switch (level) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO";
    case Level::Warning: return "WARN";
    case Level::Error: return "ERROR";
    case Level::Fatal: return "FATAL";
    default: return "OFF";
    }
}

inline const char* level_color(Level level) {
    switch (level) {
    case Level::Debug: return "\033[90m";
    case Level::Info: return "\033[36m";
    case Level::Warning: return "\033[33m";
    case Level::Error: return "\033[31m";
    case Level::Fatal: return "\033[1;31m";
    default: return "";
    }
}

inline void write_prefix(FILE* stream, Level level, const char* tag) {
    if (color_enabled()) {
        std::fprintf(stream, "%s[%s]\033[0m", level_color(level), level_name(level));
        if (tag && *tag) std::fprintf(stream, " \033[90m[%s]\033[0m", tag);
    } else {
        std::fprintf(stream, "[%s]", level_name(level));
        if (tag && *tag) std::fprintf(stream, " [%s]", tag);
    }
    std::fputc(' ', stream);
}

inline void write_message(FILE* stream, Level level, const char* tag, const char* format, std::va_list args,
                          bool newline) {
    std::va_list measure;
    va_copy(measure, args);
    char stack_buffer[1024];
    const int needed = std::vsnprintf(stack_buffer, sizeof(stack_buffer), format, measure);
    va_end(measure);
    if (needed < 0) return;

    std::string message;
    if (static_cast<size_t>(needed) < sizeof(stack_buffer)) {
        message.assign(stack_buffer, static_cast<size_t>(needed));
    } else {
        message.resize(static_cast<size_t>(needed) + 1);
        std::vsnprintf(message.data(), message.size(), format, args);
        message.resize(static_cast<size_t>(needed));
    }

    std::lock_guard<std::mutex> lock(output_mutex());
    write_prefix(stream, level, tag);
    std::fwrite(message.data(), 1, message.size(), stream);
    if (newline && (message.empty() || message.back() != '\n')) std::fputc('\n', stream);
    std::fflush(stream);
}

} // namespace detail

inline void set_level(Level level) {
    detail::minimum_level() = level;
}
inline Level level() {
    return detail::minimum_level();
}
inline void set_color(bool enabled) {
    detail::color_enabled() = enabled;
}
inline bool color() {
    return detail::color_enabled();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
inline void write(Level level, const char* tag, const char* format, ...) {
    if (level < detail::minimum_level() || level == Level::Off) return;
    std::va_list args;
    va_start(args, format);
    detail::write_message(detail::stream_for(level), level, tag, format, args, true);
    va_end(args);
}

// Starts a line without a newline so raw() fragments can complete it.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
inline void begin(Level level, const char* tag, const char* format, ...) {
    if (level < detail::minimum_level() || level == Level::Off) return;
    std::va_list args;
    va_start(args, format);
    detail::write_message(detail::stream_for(level), level, tag, format, args, false);
    va_end(args);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline void raw(const char* format, ...) {
    std::va_list args;
    va_start(args, format);
    std::lock_guard<std::mutex> lock(detail::output_mutex());
    std::vfprintf(stderr, format, args);
    va_end(args);
}

// Stream-style facade for call sites that already build output with `<<`.
class Line {
public:
    Line(Level level, const char* tag) : m_level(level), m_tag(tag) {}
    ~Line() {
        const std::string text = m_stream.str();
        if (!text.empty()) write(m_level, m_tag, "%s", text.c_str());
    }
    Line(const Line&) = delete;
    Line& operator=(const Line&) = delete;

    template <typename T> Line& operator<<(const T& value) {
        m_stream << value;
        return *this;
    }

private:
    Level m_level;
    const char* m_tag;
    std::ostringstream m_stream;
};

struct NullLine {
    template <typename T> NullLine& operator<<(const T&) { return *this; }
};

} // namespace rbrew::log

#ifdef RBREW_DISABLE_LOGGING
#define RBREW_LOG_DEBUG(...) ((void)0)
#define RBREW_LOG_INFO(...) ((void)0)
#define RBREW_LOG_WARN(...) ((void)0)
#define RBREW_LOG_ERROR(...) ((void)0)
#define RBREW_LOG_FATAL(...) ((void)0)
#define RBREW_LOG_DEBUG_BEGIN(...) ((void)0)
#define RBREW_LOG_INFO_BEGIN(...) ((void)0)
#define RBREW_LOG_WARN_BEGIN(...) ((void)0)
#define RBREW_LOG_ERROR_BEGIN(...) ((void)0)
#define RBREW_LOG_FATAL_BEGIN(...) ((void)0)
#define RBREW_LOG_RAW(...) ((void)0)
#define RBREW_LOG_STREAM(level, tag) ::rbrew::log::NullLine()
#else
#define RBREW_LOG_DEBUG(tag, ...) ::rbrew::log::write(::rbrew::log::Level::Debug, tag, __VA_ARGS__)
#define RBREW_LOG_INFO(tag, ...) ::rbrew::log::write(::rbrew::log::Level::Info, tag, __VA_ARGS__)
#define RBREW_LOG_WARN(tag, ...) ::rbrew::log::write(::rbrew::log::Level::Warning, tag, __VA_ARGS__)
#define RBREW_LOG_ERROR(tag, ...) ::rbrew::log::write(::rbrew::log::Level::Error, tag, __VA_ARGS__)
#define RBREW_LOG_FATAL(tag, ...) ::rbrew::log::write(::rbrew::log::Level::Fatal, tag, __VA_ARGS__)
#define RBREW_LOG_DEBUG_BEGIN(tag, ...) ::rbrew::log::begin(::rbrew::log::Level::Debug, tag, __VA_ARGS__)
#define RBREW_LOG_INFO_BEGIN(tag, ...) ::rbrew::log::begin(::rbrew::log::Level::Info, tag, __VA_ARGS__)
#define RBREW_LOG_WARN_BEGIN(tag, ...) ::rbrew::log::begin(::rbrew::log::Level::Warning, tag, __VA_ARGS__)
#define RBREW_LOG_ERROR_BEGIN(tag, ...) ::rbrew::log::begin(::rbrew::log::Level::Error, tag, __VA_ARGS__)
#define RBREW_LOG_FATAL_BEGIN(tag, ...) ::rbrew::log::begin(::rbrew::log::Level::Fatal, tag, __VA_ARGS__)
#define RBREW_LOG_RAW(...) ::rbrew::log::raw(__VA_ARGS__)
#define RBREW_LOG_STREAM(level, tag) ::rbrew::log::Line(level, tag)
#endif
