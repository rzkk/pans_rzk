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
    // 子类完成后具体的 ， 子类不加锁 ， 父类加锁  
    virtual void writeUnlocked(std::string_view formatted_record) = 0;
    virtual void flushUnlocked() = 0;
    virtual void syncUnlocked() = 0;
private:
    std::atomic<LogLevel::Level>m_level{LogLevel::Level::LOG_LV_DEBUG};
    std::mutex m_mutex; 
    /*notes 需要放到public 吗  
    答 ： 应该放到private
    非虚接口模式（NVI，Non-Virtual Interface) : 
        父类负责维持共同的不变量，例如线程安全、日志等级检查和异常边界；子类只负责具体输出。
        加锁是父类干的事 ， 不应该把锁暴露给子类
    */
};

namespace detail
{
//为了管理appender , 创建appender  ,相当于又封装一层 ， 后面的logger 会调用AppenderAccess来管理
// using AppenderPtr = std::shared_ptr<Appender>;
// 这是奇异递归模板吗 ，答 ： 不是 
/*notes： 这里是 Friend Accessor（友元访问代理） ： 解决的是访问权限问题：
    Appender 对外暴露 setLevel()、flush()、sync() 等接口，而允许框架内部的 AppenderAccess 访问私有构造函数和 m_impl。
    这种方式适合你的框架，因为最终用户不应该任意绕过 Logger，直接向 Appender 写入已经格式化的日志。
    需要理解的是，friend 属于 C++ 编译期访问控制，不是运行时安全隔离。它的主要价值是维护模块的封装边界。

奇异递归模板 ： 派生类把自身类型作为模板参数传给基类。
template<typename Derived>
class Base {
public:
    void interface() {
        static_cast<Derived*>(this)->implementation();
    }
};

class Derived : public Base<Derived> {
public:
    void implementation() {}
};
*/
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