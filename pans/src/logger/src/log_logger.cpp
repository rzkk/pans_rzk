#include"logger/include/log_logger_impl.h"
#include"logger/include/log_appender_impl.h"

#include<algorithm>
#include<mutex>
#include<string_view>
#include<memory>
#include <cstdio>
#include<shared_mutex>
#include<unordered_map>

namespace pans{

constexpr std::string_view DEFAULT_LOG_PATTERN = "%d{%Y-%m-%d %H:%M:%S}.%u%Tthread=%t%Tfiber=%F%T[%p]%T%f:%l%T%m%n";
/// @brief Logger的实现 ， 全是依托 impl 
/// @param name 
Logger::Logger(std::string name):m_impl(std::make_unique<Impl>(std::move(name))){}

Logger::~Logger() = default;

bool Logger::shouldLog(LogLevel::Level level)const noexcept{
    return m_impl->shouldLog(level);
}

void Logger::setLevel(LogLevel::Level level)noexcept{
    m_impl->setLevel(level);
}
LogLevel::Level Logger::getLevel()const noexcept{
    return m_impl->getLevel();
}

 [[nodiscard]] std::string_view Logger::getName() const noexcept{
    return m_impl->getName();
 }

 void Logger::setFormatter(std::string_view pattern){
    auto formatter = std::make_shared<const detail::Formatter>(pattern);
    m_impl->setFormatter(formatter);
 }

std::string Logger::getFormatter()const noexcept{
    // ? 这里的m_impl->getFormatter() 有可能得到空吗
    const  auto formatter =   m_impl->getFormatter();
    return formatter== nullptr ? std::string() : formatter->getPattern();
}
void Logger::addAppender(AppenderPtr appender){
    m_impl->addAppender(std::move(appender));
}

void Logger::removeAppender(const AppenderPtr &appender){
    m_impl->removeAppender(appender);
}

void Logger::clearAppenders(){
    m_impl->clearAppender();
}
void Logger::flush(){
    m_impl->flush();
}

void Logger::sync(){
    m_impl->sync();
}

/////下面是 Logger::Impl 的实现 
//mark 因为调用端的string name 的声明周期不长  ， 所以不能用string_view 
Logger::Impl::Impl(std::string name):m_name(std::move(name)){
    if(m_name.empty()){
        throw std::invalid_argument("logger name cannot be empty");
    }
    m_formatter = std::make_shared<const detail::Formatter>(DEFAULT_LOG_PATTERN); // 先整上默认值
}

//mark 这里有没有可能需要 memory_order_acquire 
LogLevel::Level Logger::Impl::getLevel()const noexcept{
    return m_level.load(std::memory_order_relaxed);
}
//mark 这里有没有可能需要 memory_order_release
void  Logger::Impl::setLevel(LogLevel::Level level) noexcept{
     m_level.store( level, std::memory_order_relaxed);
}

bool Logger::Impl::shouldLog(LogLevel::Level level) /*const*/ noexcept{    // ? 这里因为加锁 ， 所以不能对函数加const 吗
    if(static_cast<std::uint8_t>(level) < static_cast<std::uint8_t>(getLevel()) ){
        return false;
    }
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex); // 加读锁
        // std::shared_lock<std::shared_mutex> lock(m_mutex);

        if(!m_appenders.empty()){        // std::vector<pans::AppenderPtr> ， 有读有改 要加锁
            return true;
        }
    }
    //能达到日期器的level 要求，但是这个日志器没有目的地 ， 转到 root 求助
    return m_root != nullptr && m_root->shouldLog(level); 
}

void Logger::Impl::submit(const detail::LogRecordView & record)  noexcept{
    if(static_cast<std::uint8_t>(record.m_level) < static_cast<std::uint8_t>(getLevel()) ){
        return ;
    }
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    if(m_appenders.empty()){
        lock.unlock();
        if(m_root != nullptr){     // 自己没有目的地 ， 交给root去做
            m_root->m_impl->submit(record);
        }
        return ;
    }
    // InlineBuffer，它遇到长日志的时候，可能会使用堆内存
    // vector开辟堆内存的时候，会有极小概率抛bad_alloc异常，这里不能让异常逃逸出去
    try{
        //using FormattedRecordBuffer = InlineBuffer<FORMATTED_RECORD_INLINE_CAPACITY>;
        detail::FormattedRecordBuffer formatted_record;
        m_formatter->format(record , formatted_record);
        for(const AppenderPtr & appender : m_appenders){
            detail::AppenderAccess::Append(appender , record.m_level , formatted_record.view());
        }
    }catch(...){
        constexpr std::string_view message = "pans logger: record formatting failed\n";
        std::fwrite(message.data(), 1, message.size(), stderr);
    }

}

std::string_view Logger::Impl::getName()const noexcept{
    return m_name;
}

void Logger::Impl::setFormatter(std::shared_ptr<const detail::Formatter>formatter){
    ASSERT_RETNONE(formatter != nullptr ,"logger formatter cannot be null" ) ;

    std::unique_lock<std::shared_mutex>lock(m_mutex); //?  unique_lock 是写锁 ， shared_lock 是读锁吗 
    m_formatter = std::move(formatter);
}
std::shared_ptr<const detail::Formatter> Logger::Impl::getFormatter()/*const*/ noexcept{  //? 加锁就不能 const 
     std::shared_lock<std::shared_mutex>lock(m_mutex);
    return m_formatter;
}
//using pans::AppenderPtr = std::shared_ptr<pans::Appender>
//mark 这里使用shared_ptr是因为 一个目的地要给多个 日志器 // 那m_appenders 要存副本吗 ？ 
//? 需要等组装的时候 看看怎么调用的
void Logger::Impl::addAppender(AppenderPtr appender){
    ASSERT_RETNONE(appender != nullptr , "logger appender cannot be null");
    std::unique_lock<std::shared_mutex>lock(m_mutex);
    m_appenders.push_back(std::move(appender));
}

void Logger::Impl::removeAppender(const AppenderPtr & appender){
    std::unique_lock<std::shared_mutex>lock (m_mutex);
    std::erase(m_appenders , appender); 
}
void Logger::Impl::clearAppender(){
    std::unique_lock<std::shared_mutex>lock (m_mutex);
    m_appenders.clear();
}

void Logger::Impl::flush(){   // 调用 各个appender的flush 
    std::shared_lock<std::shared_mutex>lock(m_mutex);  // 读锁
    if(m_appenders.empty()){
        lock.unlock();
        if(m_root != nullptr){
            m_root->flush();
        }
        return ;
    }
    for(const AppenderPtr& appender : m_appenders){
        appender->flush();
    }
}

void Logger::Impl::sync(){   // 调用 各个appender的flush 
    std::shared_lock<std::shared_mutex>lock(m_mutex);  // 读锁
    if(m_appenders.empty()){
        lock.unlock();
        if(m_root != nullptr){
            m_root->sync();
        }
        return ;
    }
    for(const AppenderPtr& appender : m_appenders){
        appender->sync();
    }
}

void Logger::Impl::setRoot(const LoggerPtr & root)noexcept{
    m_root = root;
}

/////// 下面是 LoggerAccess 
void detail::LoggerAccess::Submit(Logger& logger, const LogRecordView& record) noexcept{
    logger.m_impl->submit(record);  //LoggerAccess 是Logger的友元类 
}
// 给logger设置 root 
void detail::LoggerAccess::SetRoot(Logger& logger, const LoggerPtr&root) noexcept{
    logger.m_impl->setRoot(root);  //LoggerAccess 是Logger的友元类 
}

////////////////////
 //管理所有的logger 
class LoggerManager final{
public:
    LoggerManager():m_root(std::make_shared<Logger>("root")){
        m_root->addAppender(MakeStdoutAppender());
        m_loggers.emplace( "root" , m_root );
    }
    [[nodiscard]] LoggerPtr getRoot() const noexcept{
        return m_root;
    }

     [[nodiscard]] LoggerPtr getLogger(std::string_view name)  noexcept{
        ASSERT_RETVAL(!name.empty() , nullptr , "logger name cannot be empty");
        std::lock_guard<std::mutex>lcok(m_mutex);
        const auto iterator = m_loggers.find(std::string(name));
        if(iterator != m_loggers.end()){
            return iterator->second;
        }
        //没找到 就加入一个
        auto logger = std::make_shared<Logger>(std::string(name));

        //logger.m_impl->setRoot(root); // LoggerManager 不是 Logger的友元类
        detail::LoggerAccess::SetRoot(*logger, m_root);//LoggerAccess 是Logger的友元类 
        m_loggers.emplace(logger->getName() , logger);

        return logger;
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string , LoggerPtr>m_loggers;
    LoggerPtr m_root;
};

LoggerManager& GetLoggerManager(){ // 保证单例
    static LoggerManager mgr;
    return mgr;
}
LoggerPtr GetRootLogger(){
    return GetLoggerManager().getRoot();
}   

LoggerPtr GetLogger(std::string_view name){
    return GetLoggerManager().getLogger(name);
}

} // namespace pans

