#ifndef PANS_INCLUDE_PANS_LOGGER_APPENDER_H
#define PANS_INCLUDE_PANS_LOGGER_APPENDER_H


#include<memory>
#include<string>

#include<pans/export.h>   // 控制 函数 类的暴露范围
#include<pans/logger/log_level.h>

namespace pans
{

namespace detail
{
//用来代理我们的内部权限，严格控制谁能访问我们的Appender私有实现
//接口与实现隔离 ， 暴露在外面的 只有接口
class AppenderAccess; // 这里只做前置声明 
} // namespace detail

class PANS_API Appender final{
public:
    class Impl; // 内部类的前向声明
    //前向声明可以让你持有某个类型的指针，但不代表所有涉及该类型的操作都可以在类型不完整时完成。
    Appender(const Appender &) = delete;
    Appender& operator=(const Appender&) = delete;
    Appender(Appender &&) = delete;
    Appender& operator=(Appender&&) = delete;

    
    //~Appender() = default;
    // 因为这里  std::unique_ptr<Impl>m_impl; 中的Impl 只是前向声明， 没有完整定义 
    ////std::unique_ptr<Impl> 允许 Impl 是不完整类型
    //但使用默认删除器销毁对象时，Impl 必须已经是完整类型。
    ~Appender();

    //不同Appender有不同的日志等级: appender的等级控制要不要在这个目的地输出
    void setLevel(LogLevel::Level level) noexcept;
    [[nodiscard]] LogLevel::Level getLevel() const noexcept;

    void flush();// 日志缓冲区(进程流的缓冲区) -> 操作系统的脏页  
    void sync(); // 操作系统脏页 -> 磁盘缓冲区

private:
    explicit Appender( std::unique_ptr<Impl> )noexcept;
    //std::unique_ptr<Impl> 允许 Impl 是不完整类型

    std::unique_ptr<Impl>m_impl;
    friend class detail::AppenderAccess;
    //AppenderAccess 中的append 会调用 m_impl 
};

using AppenderPtr = std::shared_ptr<Appender>;
PANS_API AppenderPtr MakeStdoutAppender();
PANS_API AppenderPtr MakeFileAppender(std::string file_name);


} // namespace pans












#endif

