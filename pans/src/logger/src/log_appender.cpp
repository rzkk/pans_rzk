#include "logger/include/log_appender_impl.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <pans/macros.h>

// 用文件的底层接口
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace pans{
// Appender具体实现 ， 主要是调接口
/*notes :noexcept 在声明与实现中都需要带着 */
Appender::Appender(std::unique_ptr<Impl>impl)noexcept:m_impl(std::move(impl)) {}

void Appender::setLevel(LogLevel::Level level)noexcept{
    m_impl->setLevel(level);
}
LogLevel::Level Appender::getLevel()const noexcept{
    return m_impl->getLevel();
}
void Appender::flush(){
    m_impl->flush();
}
void Appender::sync(){
    m_impl->sync();
}

Appender::~Appender() = default;
///////
//Appender::Impl 具体实现
void Appender::Impl::setLevel(LogLevel::Level level)noexcept{
    m_level.store(level , std::memory_order_relaxed); //  需要 memory_order_release 吗
}
LogLevel::Level Appender::Impl::getLevel()const noexcept{
    return  m_level.load( std::memory_order_relaxed); //  需要 memory_order_acquire 吗
}
/*notes：  
不需要 memory_order_acquire
std::memory_order_relaxed 足够 ， m_level 有默认值 DEBUG 
*/
void Appender::Impl::flush(){
    std::lock_guard<std::mutex>lock(m_mutex);
    flushUnlocked();
}
void Appender::Impl::sync(){
    std::lock_guard<std::mutex>lock(m_mutex);
    syncUnlocked();
}
void Appender::Impl::append(LogLevel::Level level , std::string_view formatted_record)noexcept{
    if(static_cast<std::uint8_t>(level) < static_cast<std::uint8_t>(getLevel()) ){
        return ;
    }
    // 因为整个函数appender需要 noexcept的 ， 所以需要把函数体内的异常接住
    // std::mutex::lock()有可能抛出 std::system_error异常
    // lock_guard 也不是noexcept 的
    try{
        std::lock_guard<std::mutex>lock(m_mutex);
        writeUnlocked(formatted_record); // 子类重载的方法
        if(level == LogLevel::Level::LOG_LV_FATAL){
            flushUnlocked();
        }
    }catch(...){
        constexpr std::string_view message = "pans logger: appender operation failed\n";
        std::fwrite(message.data() , 1, message.size() , stderr);
        /*notes: fwrite 
        1 <cstdio>
        2 std::size_t fwrite(const void* buffer, std::size_t size, 
                            std::size_t count, std::FILE* stream);
        3   数据源指针（buffer）： 指向内存中待写入数组中首个对象的指针（const void*），函数会从该地址开始按字节读取数据。
            单个对象大小（size）： 每个待写入元素的字节大小，通常直接使用 sizeof 运算符获取。
            元素个数（count）： 期望写入的元素数量，单次调用计划写入的总字节数为 size 与 count 的乘积。 
            目标文件流（stream）： 指向输出流对象的指针，可以是 std::fopen 打开的文件指针，也可以是 stdout 或 stderr 等标准流。
        4   返回成功写入的元素个数
        */
    }
}
// 接下来具体的 目的地
class StdoutAppenderImpl final: public Appender::Impl{
protected:
    // ? 这里的 write 是 noexcept的吗
    /*notes : std::cout.write 不是noexcept  ， 所以应该去掉writeUnlocked 的 noexcept */ 
    virtual void writeUnlocked(std::string_view formatted_record) noexcept override{
        std::cout.write( formatted_record.data(),static_cast<std::streamsize>(formatted_record.size()));
        
        /*notes :std::cout.write
        1 #include <iostream>  定义于 std::basic_ostream 类模板中
            std::ostream& write(const char* s, std::streamsize count);
        2   数据源指针（s）： 指向待写入字符数组或内存块起始位置的指针（类型为 const char*）；
                    若要输出非字符类型的二进制数据，必须使用 reinterpret_cast<const char*> 进行显式转换。
            写入字节数（count）： 期望连续写入的字节（字符）数量，类型为 std::streamsize（本质上是有符号整型）。
        3 无视空字符（'\0'）截断： 常规的 std::cout << str 遇到第一个空字符 '\0' 就会停止输出；
                    而 std::cout.write(str, count) 只看字节数 count，即便数据中间包含多个 '\0' 也会完整写出。    
        4 C 语言的 std::fwrite 接收 const void*，任何类型的指针都能直接传入；而 C++ 的 write 成员函数严格要求 const char*，直接传入整型数组或结构体指针会导致编译报错。
        */
    }
    virtual void flushUnlocked()  override{
        std::cout.flush();
    }
    virtual void syncUnlocked()  override{
        flushUnlocked();  //  直接调用 刷新函数
    }
};
class FileAppenderImpl final: public Appender::Impl{
public:
    explicit FileAppenderImpl(const std::string& file_name){
        if(file_name.empty()){
             throw std::invalid_argument("logger file name cannot be empty");
        }
        //a : append ,追加模式
        //b : 二进制形式 ，原始字节写入，在windows 上可以避免换行转换 ， win上换行需要两个字符(查一查) ， linux 上没有啥用
        // file_name.c_str() 返回的C 风格字符串没有 \0 , fopen 需要的字符串应该需要 \0 ， 不会有bug吗
        /*notes：
        std::string file_name = "server.log"; 
        调用 ： file_name.c_str()  得到 s e r v e r . l o g \0 
         \0 是C++ 标准保证的
         data() 也有相同的 \0    
        */
        m_file = std::fopen(file_name.c_str() , "ab");
        if(m_file == nullptr){
              throw std::system_error(errno, std::generic_category(), "failed to open logger file: " + file_name);
        }
    }
    ~FileAppenderImpl() override{
        if(m_file != nullptr){
            std::fflush(m_file);
            std::fclose(m_file);
        }
    }
protected:
    //// 这里的 fwrite 是 noexcept的吗
    /*notes :  C 标准 I/O 函数 , 不会抛异常， 只会使用返回值及错误状态。
        但是不管抛不抛 ， 都可以由外层统一捕获
    */
   
    virtual void writeUnlocked(std::string_view formatted_record)  override{
       std::fwrite(formatted_record.data() , 1 , formatted_record.size() , m_file);
    }
    virtual void flushUnlocked()  override{
        std::fflush(m_file);
    }
    virtual void syncUnlocked()  override{
        flushUnlocked();
#if defined(_WIN32)
        ::_commit(::_fileno(m_file));
#else 
        ::fdatasync(::fileno(m_file));
        /*notes  ::fdatasync(::fileno(m_file));
         1 是 C++（在 Linux/POSIX 环境下）中一段典型的强制数据落盘代码。
         它的核心作用是：从 C 标准库的文件流指针 m_file（FILE*）中提取出操作系统底层的整型文件描述符（File Descriptor），
         并调用内核系统调用将该文件在内核缓冲区（Page Cache）中的数据强制刷新到物理磁盘上，以确保系统断电或崩溃时数据不会丢失
         2    #include <cstdio>   // 提供 ::fileno
            #include <unistd.h> // 提供 ::fdatasync
         3 数据同步系统调用（::fdatasync）： 接收文件描述符 fd，阻塞当前线程并通知操作系统内核将该文件所有的脏页（Dirty Pages）
                                            刷入物理存储设备（SSD/HDD）的非易失性介质中。
        4 fdatasync 与 fsync 的性能差异
            全量同步（fsync）： 每次调用不仅会将文件数据写回磁盘，还会强制更新文件的所有元数据（Metadata，如修改时间 mtime、访问时间 atime、权限属性等），
                                因为文件数据和 inode 元数据通常存放在磁盘的不同位置，往往会导致额外的磁盘 I/O 开销。
            按需同步（fdatasync）： 只强制刷新文件数据本身以及后续正确读取该数据所必需的元数据（例如文件大小发生改变时才同步 inode），跳过单纯的时间戳更新，
                                因此在数据库事务日志（WAL）和高吞吐消息队列中性能明显优于 fsync。
        5 调用前必须先执行 std::fflush(m_file)： std::fwrite 写入的数据首先停留在 C 标准库的用户态内存缓冲区中，
                        而 ::fdatasync 只负责将内核态 Page Cache 刷入物理磁盘；若直接调用 ::fdatasync(::fileno(m_file)) 而不先调用 fflush，
                        滞留在用户态的数据根本没有进入内核，自然也不会落盘。

        6 切勿忽略返回值检查： 工程代码中直接丢弃返回值的写法存在隐患，当磁盘故障、空间不足或流无效时，函数会返回 -1 并设置全局变量 errno（如 EIO 或 EBADF），
                                应当捕获并处理异常。

        7 跨平台可移植性限制： fileno 和 fdatasync 属于 POSIX 标准接口而非 C++ 标准库函数；
                            在 Windows 平台上编译这段代码会报错，需替换为 MSVC 提供的 _fileno(m_file) 与 _commit(fd)。
        */
#endif
    }
private:
    std::FILE* m_file = nullptr;
    //  因为  std::ofstream C++20 没法获取底层的文件描述符 ， 比较难实现sync 和flush 接口
    //  使用 FILE*  会很方便实现sync 和flush 接口 ， 调用fwrite 和fflush 不会抛异常 ， 只会返回值 和设置 errorno
};

//using pans::AppenderPtr = std::shared_ptr<pans::Appender> 
AppenderPtr detail::AppenderAccess::MakeStdoutAppender(){
    return AppenderPtr(new Appender(std::make_unique<StdoutAppenderImpl>()));
}

AppenderPtr detail::AppenderAccess::MakeFileAppender(std::string file_name){
    return AppenderPtr(new Appender(std::make_unique<FileAppenderImpl>(file_name)));
}

void detail::AppenderAccess::Append(const AppenderPtr& appender, LogLevel::Level level, std::string_view formatted_record)noexcept{
    if(appender!= nullptr){
        appender->m_impl->append(level , formatted_record);
    }
}

// 全局函数 用来创建Appender
AppenderPtr MakeStdoutAppender()
{
    return detail::AppenderAccess::MakeStdoutAppender();
}

AppenderPtr MakeFileAppender(std::string file_name)
{
    return detail::AppenderAccess::MakeFileAppender(std::move(file_name));
}


} // namespace pans 