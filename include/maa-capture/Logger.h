#pragma once

// ============================================================================
// 极简日志工具（替代原 MaaUtils/Logger.h）
//
// 独立版不依赖 MaaUtils，提供与原版语义一致的流式宏：
//     LogInfo << "hello" << VAR(x) << VAR_VOIDP(hwnd);
// 输出到 stderr，带时间戳与级别。
//
// 如需重新接入 MaaFramework，可将本文件的实现替换为对 MaaUtils Logger 的转发，
// 调用处的 LogXxx / VAR / VAR_VOIDP 写法保持不变。
// ============================================================================

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

#include "Common.h"

MWS_NS_BEGIN

enum class LogLevel
{
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

namespace detail
{
    inline const char* level_name(LogLevel level)
    {
        switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        default:
            return "?????";
        }
    }

    inline void write_log(LogLevel level, const std::string& msg)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf {};
#if defined(_WIN32)
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        std::ostringstream oss;
        oss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms
            << "][" << level_name(level) << "] " << msg << "\n";
        std::fputs(oss.str().c_str(), stderr);
    }
}

// 可替换的日志接收器：默认为 stderr；集成方可覆盖此函数指针
inline void (*g_log_sink)(LogLevel level, const std::string& msg) = &detail::write_log;

class LoggerStream
{
public:
    explicit LoggerStream(LogLevel level)
        : level_(level)
    {
    }

    ~LoggerStream()
    {
        if (g_log_sink) {
            g_log_sink(level_, ss_.str());
        }
    }

    LoggerStream(const LoggerStream&) = delete;
    LoggerStream& operator=(const LoggerStream&) = delete;

    template <typename T>
    LoggerStream& operator<<(T&& value)
    {
        ss_ << std::forward<T>(value);
        return *this;
    }

    LoggerStream& operator<<(std::ostream& (*fn)(std::ostream&))
    {
        fn(ss_);
        return *this;
    }

private:
    LogLevel level_;
    std::ostringstream ss_;
};

MWS_NS_END

#define LogDebug ::mws::LoggerStream(::mws::LogLevel::Debug)
#define LogInfo ::mws::LoggerStream(::mws::LogLevel::Info)
#define LogWarn ::mws::LoggerStream(::mws::LogLevel::Warn)
#define LogError ::mws::LoggerStream(::mws::LogLevel::Error)

// 与原 MaaUtils 的 VAR 语义一致：展开为 " 名字=值" 的流式片段
#define VAR(x) " " #x "=" << (x)
#define VAR_VOIDP(x) " " #x "=" << static_cast<void*>(x)

#define LogFunc ::mws::LoggerStream(::mws::LogLevel::Debug) << "func: " << __FUNCTION__ << " | "