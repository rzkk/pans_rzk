// 日志记录视图 
// 日志记录视图 ： 捕捉日志发生的那一刻 ， 系统中主要关注的参数

#ifndef PANS_SRC_LOGGER_LOG_RECORD_H
#define PANS_SRC_LOGGER_LOG_RECORD_H


#include <chrono>
#include<string_view>
#include<cstdint>  // 主要提供一组具有明确位宽的整数类型。 std::uint16_t 
// #include<source_location> // 记录文件位置的  C++20    // 获取绝对路径

#include <pans/logger/log_level.h>
#include <pans/macros.h>

namespace pans::detail
{

class LogRecordView{
public:
    LogLevel::Level m_level = LogLevel::Level::LOG_LV_DEBUG;
    std::string_view m_loggerName = ""; // 日志器的名字, 可以有多个日志器'
    std::string_view m_message = ""; // 日志内容

    std::chrono::system_clock::time_point   m_timestamp; //时间戳
    std::chrono::steady_clock::duration     m_elapsed;  //  启动服务器到现在多久 默认是 nanoseconds  纳秒
    //typedef std::chrono::nanoseconds std::chrono::_V2::steady_clock::duration
    
    std::uint64_t m_threadId = 0;  // 线程ID
    std::uint64_t m_fiberId  = 0;  // 协程ID 

    std::string_view m_threadName = ""; // 线程名
    std::string_view m_fileName = ""; // 文件名

    std::uint64_t m_line = 0; // 文件名 + 行号 定位 

    // std::source_location  m_location ; // C++ 20 ， 表示文件位置 ， 编译器完成 ， 编译期完成 运行时0消耗 
    //已知文件名和 行号 ， 就不需要 函数名了
};

} // namespace pans::detail






















#endif


