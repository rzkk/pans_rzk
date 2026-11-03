#ifndef PANS_SRC_LOGGER_APPNEDER_IMPL_H
#define PANS_SRC_LOGGER_APPNEDER_IMPL_H

#include <atomic>
#include<cstdio>

#include<mutex>
#include<string_view>
#include<string>


#include<pans/logger/log_appender.h>

namespace pans
{
//Appender 内部类 Impl 的实现 
//后面会派生具体的目的类 ：  FileAppenderImpl  StdoutAppenderImpl
class Appender::Impl{   
public:
    virtual ~Impl() = default;

    //写日志
    //formatted_record :已经格式化号的日志
    void append(LogLevel::Level level , std::string_view formatted_record)noexcept;

    void setLevel(LogLevel::Level level) noexcept;

    [[nodiscard]] LogLevel::Level getLevel() const noexcept;
    
    void flush();
    void sync();
protected:
    //在上层加锁， 写不加锁
    virtual void writeUnlocked(std::string_view formatted_record) noexcept= 0;
    virtual void flushUnlocked() noexcept= 0;
    virtual void syncUnlocked() noexcept= 0;
private:
    std::atomic<LogLevel::Level>m_level{LogLevel::Level::LOG_LV_DEBUG};
    std::mutex m_mutex; // ? 需要放到public 吗 
};

namespace detail
{
//为了管理appender , 创建appender  ,相当于又封装一层 ， 后面的logger 会调用AppenderAccess来管理
// using AppenderPtr = std::shared_ptr<Appender>;
// ? 这是奇异递归模板吗 ， 详细解释
class AppenderAccess final{
public:
    [[nodiscard]] static AppenderPtr MakeStdoutAppender();
    [[nodiscard]] static AppenderPtr MakeFileAppender(std::string file_name);
    // 写日志接口 : 往哪个appender写
    static void Append(const AppenderPtr & appender , LogLevel::Level level , std::string_view formatted_record)noexcept;
};
    
} // namespace detail




} // namespace pans





#endif