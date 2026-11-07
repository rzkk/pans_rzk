#ifndef PANS_INCLUDE_PANS_LOGGER_LOG_H
#define PANS_INCLUDE_PANS_LOGGER_LOG_H

#include <cstddef>
#include <iosfwd>  // ????查 ， 只有轻量的头文件？？？？？？？？
#include <string_view>

#include <pans/logger/log_appender.h>
#include <pans/logger/log_level.h>
#include <pans/logger/log_logger.h>
#include <pans/macros.h>

//namespace :   pans 外部调用者可见的东西 
//              detail ： 不应该对外部者开放 ， 对编译器可见
// PANS_API ：如果被作为动态库编译 ， 
// LogLine：在构造函数中捕捉日志 ， 析构中打印日志


namespace pans::detail
{

// 在那一行产生了日志 
class PANS_API LogLine final{
public:
    // ? line是有什么用
    LogLine(Logger&logger, LogLevel::Level level , std::uint32_t line , std::string_view file_name);
    ~LogLine() noexcept;

    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;
    LogLine(LogLine&&) = delete;
    LogLine& operator=(LogLine&&) = delete;

    [[nodiscard]] std::ostream& stream() noexcept; // 应对流式接口


private:
    // 只声明，没有指针指向  // logLine 的构造频率很高 ， 需要在栈上分配
    struct Impl;  // 其含有InlineBuffer ,  内存大小未知 
    static constexpr std::size_t LOG_LINE_IMPL_SIZE = 1024;
    // 要求数组满足平台的对齐规则最大的对齐
    alignas(std::max_align_t) std::byte m_implStorage[LOG_LINE_IMPL_SIZE];  // 在m_implStorage 上构造 LogLine::Impl
    [[nodiscard]] Impl& getImpl() noexcept;
};

PANS_API void LogPrintf(Logger& logger, LogLevel::Level level, std::uint32_t line, 
                std::string_view file_name, const char* format, ...);
} // namespace pans::detail

// logger 有可能是表达式 ， auto pans_log_logger = (logger) ,赋值出来 ， 表达式只需要执行一次 
#define PANS_LOG_LEVEL(logger , level) \
    if(auto pans_log_logger = (logger) ; !pans_log_logger){} \
    else if (const auto pans_log_level = (level); !pans_log_logger->shouldLog(pans_log_level) ){} \
    else pans::detail::LogLine(*pans_log_logger , pans_log_level , __LINE__,__FILE__).stream()  

#define PANS_LOG_DEBUG(logger) PANS_LOG_LEVEL((logger) , pans::LogLevel::Level::LOG_LV_DEBUG )
#define PANS_LOG_INFO(logger) PANS_LOG_LEVEL((logger), pans::LogLevel::Level::LOG_LV_INFO)
#define PANS_LOG_WARN(logger) PANS_LOG_LEVEL((logger), pans::LogLevel::Level::LOG_LV_WARN)
#define PANS_LOG_ERROR(logger) PANS_LOG_LEVEL((logger), pans::LogLevel::Level::LOG_LV_ERROR)
#define PANS_LOG_FATAL(logger) PANS_LOG_LEVEL((logger), pans::LogLevel::Level::LOG_LV_FATAL)


// printf 格式的输出  // format :    "c=%c i=%d msg1: %s msg2: %s msg3: %s", c, i, msg1, msg2.c_str(), msg3.data();
#define PANS_LOG_FMT_LEVEL(logger , level , format,...)\
    if(auto pans_log_logger = (logger) ; !pans_log_logger){} \
    else if (const auto pans_log_level = (level); !pans_log_logger->shouldLog(pans_log_level) ){} \
    else pans::detail::LogPrintf(*pans_log_logger , pans_log_level , __LINE__, __FILE__, (format)__VA_OPT__(,) __VA_ARGS__)

#define  PANS_LOG_FMT_DEBUG(logger , format , ...)\
    PANS_LOG_FMT_LEVEL((logger), pans::LogLevel::Level::LOG_LV_DEBUG, (format) __VA_OPT__(,) __VA_ARGS__)
#define PANS_LOG_FMT_INFO(logger, format, ...) \
    PANS_LOG_FMT_LEVEL((logger), pans::LogLevel::Level::LOG_LV_INFO, (format) __VA_OPT__(,) __VA_ARGS__)
#define PANS_LOG_FMT_WARN(logger, format, ...) \
    PANS_LOG_FMT_LEVEL((logger), pans::LogLevel::Level::LOG_LV_WARN, (format) __VA_OPT__(,) __VA_ARGS__)
#define PANS_LOG_FMT_ERROR(logger, format, ...) \
    PANS_LOG_FMT_LEVEL((logger), pans::LogLevel::Level::LOG_LV_ERROR, (format) __VA_OPT__(,) __VA_ARGS__)
#define PANS_LOG_FMT_FATAL(logger, format, ...) \
    PANS_LOG_FMT_LEVEL((logger), pans::LogLevel::Level::LOG_LV_FATAL, (format) __VA_OPT__(,) __VA_ARGS__)

// 找logger
#define PANS_LOG_ROOT() pans::GetRootLogger()
#define PANS_LOG_NAME(name) pans::GetLogger((name))

#define LOG_DEBUG PANS_LOG_DEBUG(g_logger)
#define LOG_INFO  PANS_LOG_INFO(g_logger)
#define LOG_WARN  PANS_LOG_WARN(g_logger)
#define LOG_ERROR PANS_LOG_ERROR(g_logger)
#define LOG_FATAL PANS_LOG_FATAL(g_logger)

#endif // PANS_INCLUDE_PANS_LOGGER_LOG_H




/*notes : 
alignas(std::max_align_t) std::byte m_implStorage[LOG_LINE_IMPL_SIZE];  // 在m_implStorage 上构造 LogLine::Impl
    1 为什么用 alignas(std::max_align_t) 而不是 alignas(Impl)？
        答：这是 Pimpl 模式的精髓所在：这行代码写在头文件（.h）里，而在头文件中 struct Impl; 只有一个前置声明（Forward Declaration），属于不完整类型。
        编译器在编译头文件时，根本不知道 sizeof(Impl) 和 alignof(Impl) 是多少。
        因此，可以用一个宏/常量 LOG_LINE_IMPL_SIZE 预留了足够大的字节数，并用 alignas(std::max_align_t) 强制该数组按照当前平台标准标量类型的最大对齐要求（64 位系统上通常是 8 或 16 字节）进行内存对齐，
        确保后续把 Impl 放进去时不会因内存未对齐而触发硬件异常或未定义行为（UB）。 // Impl 相当于结构体， 里面很多
*/