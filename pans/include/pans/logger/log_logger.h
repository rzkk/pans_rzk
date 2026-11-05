#ifndef PANS_INCLUDE_PANS_LOGGER_LOGGER_H
#define PANS_INCLUDE_PANS_LOGGER_LOGGER_H

#include <memory>
#include <string>
#include <string_view>

#include <pans/export.h>
#include <pans/logger/log_appender.h>
#include <pans/logger/log_level.h>


namespace pans
{
namespace detail
{
class LoggerAccess;
} // namespace detail

class PANS_API Logger final{
public:
    class Impl;
    explicit Logger(std::string name = "root");
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator= (const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator= (Logger&&) = delete;

    //日志本身的level 和日志器的level
    [[nodiscard]] bool shouldLog(LogLevel::Level level) const noexcept; // 要不要构造这条日志 

    void setLevel(LogLevel::Level level)noexcept;
    [[nodiscard]] LogLevel::Level getLevel()const noexcept;
    [[nodiscard]] std::string_view getName() const noexcept; 

    void setFormatter(std::string_view pattern);  // 不同的logger 可以不同的formatter , 比如写入file 的就要尽可能详细 ， 网络发出去的就不需要知道协程号之类的信息
    [[nodiscard]] std::string getFormatter() const noexcept;
    void addAppender(AppenderPtr appender);
    
    //? 这里为什么是 & 类型
    void removeAppender(const AppenderPtr &appender);
    void clearAppenders();

    void flush();
    void sync();
private:
    std::unique_ptr<Impl>m_impl;  // ? 这里是unique_ptr
    friend class detail::LoggerAccess;
};

using LoggerPtr = std::shared_ptr<Logger>;
[[nodiscard]] PANS_API  LoggerPtr GetRootLogger();
[[nodiscard]] PANS_API  LoggerPtr GetLogger(std::string_view name);


} // namespace pans

#endif