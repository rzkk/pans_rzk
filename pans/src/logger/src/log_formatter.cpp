#include "logger/include/log_formatter.h"
#include<unordered_map>
#include<array>
#include<charconv> // std::to_chars , C++17 , 
#include<ctime> // time_t 
namespace pans::detail
{
constexpr std::string_view DEFAULT_DATE_FORMAT = "%Y-%m-%d %H:%M:%S" ;

Formatter::Formatter(std::string_view pattern):m_pattern(pattern){
    if(parse() != 0){// 日志解析失败， 无日志可用 // 失败之后 应该用默认的
        throw std::invalid_argument("invalid  logger formate pattern"); 
    }
}

void Formatter::format( const LogRecordView& record , FormattedRecordBuffer & output)const{
    // 遍历得到的 m_items，将每一个都追加到 output buffer
    //std::vector< std::unique_ptr<FormatItem> >m_items;
    for(const auto &item : m_items ){
        item->format(record , output);
    }
}
// 将数字类型转成字符串 , 限制最多 24 位整数
// std::to_string ： 会在堆上开空间 -> 不是百分百安全 ， 会抛异常
// 使用自定义的
// 因为： buffer中是按照字节存的
template<class T>
void AppendInteger(FormattedRecordBuffer& output , T value) noexcept{
    std::array<char ,24> buffer{};

    /*notes: 
    //to_chars , C++17 , <charconv> , 本身不分配堆内存 , 写入调用者提供的缓冲区
    // 不会写入 '\0'返回类型：
        struct to_chars_result
        {
            char* ptr;  // 指向转换后的最后一个字符
            errc ec;
            #if __cplusplus > 201703L && __cpp_impl_three_way_comparison >= 201907L
                friend bool
                operator==(const to_chars_result&, const to_chars_result&) = default;
            #endif
        }
  };*/
    const auto result = std::to_chars(buffer.data() , buffer.data()+buffer.size() , value );
    ASSERT_RETNONE(result.ec == std::errc() ,  "trans" << value <<" to chars  failed");
    output.append(buffer.data() , static_cast<std::size_t>(result.ptr - buffer.data()) );
}

class LiteralFormatItem final :public Formatter::FormatItem{
public:
    explicit LiteralFormatItem(std::string &&value) : m_value(std::move(value)){}
    virtual void format(const LogRecordView&  , FormattedRecordBuffer & output) const override{
        output.append(m_value);
    }
private:
    std::string m_value;
};
class MessageFormatItem final: public Formatter::FormatItem{
public:
    virtual void format(const LogRecordView& record , FormattedRecordBuffer & output) const override{
        output.append(record.m_message);
    }
};

class LevelFormatItem final : public Formatter::FormatItem{
public:
        virtual void format(const LogRecordView& record , FormattedRecordBuffer & output) const override{
            output.append( LogLevel::ToString(record.m_level));
        }
};
//开服已经过去的时间
class ElapsedFormatItem final : public Formatter::FormatItem{
public:
    virtual void format(const LogRecordView& record , FormattedRecordBuffer & output) const override{
        // milliseconds ： 毫秒
        AppendInteger(output , std::chrono::duration_cast<std::chrono::milliseconds>(record.m_elapsed).count());
        //output.append(record.m_elapsed.count());
    }
};

class LoggerNameFormatItem final : public Formatter::FormatItem{
public:
    virtual void format(const LogRecordView& record , FormattedRecordBuffer & output) const override{
        output.append(record.m_loggerName);
    }
};

class ThreadIdFormatItem final: public Formatter::FormatItem{
public:
    virtual void format(const LogRecordView& record , FormattedRecordBuffer & output) const override{
        AppendInteger(output , record.m_threadId);
    }
};

class NewLineFormatItem final : public Formatter::FormatItem{
public:
    virtual void format(const LogRecordView&  , FormattedRecordBuffer & output) const override{
        output.append('\n');
    }
};

//有可能是有很多日志 在同一秒内打出来 ： 时间戳精确到秒的转换需要做很多次 -> 同一秒的日志的时间直接复用
// 对于日志 Formatter，在构造时确定格式字符串，此后保持不可变。这样不仅更容易保证线程安全，也更容易进行缓存优化。
// 时间拆成两部分 ， 这里只到秒 
//todo : 其实可以设定 所有日志器的时间格式都一样 
class DataTimeFormatItem final: public Formatter::FormatItem{
public:
    explicit DataTimeFormatItem(std::string_view data_format)
        :m_data_format(data_format.empty() ? DEFAULT_DATE_FORMAT : data_format){}

    virtual void format(const LogRecordView&record  , FormattedRecordBuffer & output) const override{
        struct DateTimeCache {   
            std::time_t last_second {} ;// 初始化为 -1，避免 Epoch 0 时跳过格式化
            std::string last_data_format;
            char cached_text[64] = {'\0'}; 
            bool valid = false;
            std::size_t cached_size = 0;      // 缓存字符串有效长度
        };
        static thread_local DateTimeCache cache; // 每个线程拥有的 独一份

        const auto duration = record.m_timestamp.time_since_epoch();
        //const std::chrono::system_clock::duration duration = record.m_timestamp.time_since_epoch();
        //将纳秒转成  秒 
        const time_t current_second = static_cast<time_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(duration).count() 
                        ); 

        // 优先用指针比对加速同实例命中，仅在跨实例时比对格式字符串内容
        const bool format_changed = (cache.last_data_format != m_data_format);

        // 同一秒、相同格式，才复用缓存
        if(!cache.valid || cache.last_second != current_second  || format_changed){
            std::tm buffer{};
#if defined(_WIN32)
            const errno_t result = localtime_s(&buffer , &current_second);
            ASSERT_RETNONE(result == 0 , "failed to convert log time to local time ");
#else 
            // localtime_r是线程安全的，但是消耗也大：
            //_r :表示线程安全的接口 ， 会有一个全局锁 ， 全局锁就有可能要排队
            // 还需要进行复杂的时区计算 （ 特别是有夏令时变更历史的时区 ）
            // 如果系统没有加载时区信息或者要求实时响应时区变更, 每次调用这个接口还要去发起磁盘IO，读系统文件/etc/localtime
            const std::tm * result = localtime_r(&current_second , & buffer);
            ASSERT_RETNONE(result != nullptr , "failed to convert log time to local time ");
#endif
            const std::size_t size = std::strftime(cache.cached_text , sizeof(cache.cached_text) , 
                                                    m_data_format.c_str() , &buffer
                                                    ); 
            if (size == 0) {
                cache.valid = false;
                cache.cached_text[0] = '\0';
                ASSERT_RETNONE(size != 0, "failed to format log time");
            }                                        
            // 仅在格式化成功后更新缓存状态
            cache.last_second = current_second;
            cache.last_data_format = m_data_format;
            cache.cached_size = size;
            cache.valid = true;
        }
        output.append(std::string_view(cache.cached_text, cache.cached_size));
    } 

private:
    std::string m_data_format ;
} ;
// 时间的毫秒部分
class MicrosecondsFormatItem final : public Formatter::FormatItem{
public:    
    virtual void format(const LogRecordView& record , FormattedRecordBuffer & output)const  override{
        const auto total_microseconds = 
            std::chrono::duration_cast<std::chrono::microseconds>(record.m_timestamp.time_since_epoch()).count();
        std::int64_t microseconds = total_microseconds % 1000000 ;
        //if(microseconds < 0 ) microseconds += 1000000;  // duration_cast 对负数向0截断
        
        // 秒内微秒必须补齐成固定六位，否则 12:00:00.1234 这样的时间戳无法解析，也无法按字典序排序。
        std::array<char , MICROSECONDS_WIDTH> digits{};

        auto remaining = static_cast<std::uint32_t>(microseconds);

        for(std::size_t index = digits.size() ; index-- > 0  ; ){
            digits[index] = static_cast<char>('0' + remaining%10);
            remaining /= 10;
        }

        output.append(digits.data() , digits.size());
        
    }
private:
    static constexpr std::size_t MICROSECONDS_WIDTH = 6; // 常量 , 毫秒保留6位
};





class FileNameFormatItem final: public Formatter::FormatItem{
public:
     virtual void format(const LogRecordView& record , FormattedRecordBuffer & output) const override{
        output.append(record.m_fileName);
    }
};

class LineFormatItem final: public  Formatter::FormatItem{
public:
     virtual void format(const LogRecordView& record , FormattedRecordBuffer & output) const override{
        AppendInteger(output , record.m_line);
    }
};


class TableFormatItem final : public Formatter::FormatItem
{
public:
    virtual void format(const LogRecordView& , FormattedRecordBuffer& output) const override
    {
        output.append('\t');
    }
};

class FiberIdFormatItem final : public Formatter::FormatItem
{
public:
    virtual void format(const LogRecordView& record, FormattedRecordBuffer& output) const override
    {
        AppendInteger(output , record.m_fiberId);
    }
};
class ThreadNameFormatItem final : public Formatter::FormatItem
{
public:
    virtual void format(const LogRecordView&record , FormattedRecordBuffer& output) const override
    {
        output.append(record.m_threadName);
    }
};



//函数指针
using FormatItemFactory = std::unique_ptr<Formatter::FormatItem> (*)(std::string_view);
//函数工厂： 产生FormatItemFactory , 生产 Item   // 参数 是为了兼容下面的 CreateConfiguredFormatItem 
template<typename Item>
std::unique_ptr<Formatter::FormatItem> CreateSimpleFormatItem(std::string_view){
    return std::make_unique<Item>();
}
// format 给 DataTimeFormatItem 初始化用的
template<typename Item>
std::unique_ptr<Formatter::FormatItem> CreateConfiguredFormatItem(std::string_view format){
    return std::make_unique<Item>(format);
}

const std::unordered_map<char , FormatItemFactory> & GetFormatItemFactories(){
    
    static const std::unordered_map<char , FormatItemFactory>FORMAT_ITEM_FACTORIES{
        {'m' , &CreateSimpleFormatItem<MessageFormatItem>},    // 日志内容
        {'p' , &CreateSimpleFormatItem<LevelFormatItem>},      // LogLevel
        {'r' , &CreateSimpleFormatItem<ElapsedFormatItem>},    // 起服已过时间
        {'c' , &CreateSimpleFormatItem<LoggerNameFormatItem>}, // 日志器名称
        {'t' , &CreateSimpleFormatItem<ThreadIdFormatItem>},   // 线程id
        {'n' , &CreateSimpleFormatItem<NewLineFormatItem>},    // 换行
        {'d' , &CreateConfiguredFormatItem<DataTimeFormatItem>},   // datetime
        {'u' , &CreateSimpleFormatItem<MicrosecondsFormatItem>},// 毫秒数 MicrosecondsFormatItem
        {'f' , &CreateSimpleFormatItem<FileNameFormatItem>},    // 文件名
        {'l' , &CreateSimpleFormatItem<LineFormatItem>},        // 行号
        {'T' , &CreateSimpleFormatItem<TableFormatItem>},       // 制表符
        {'F' , &CreateSimpleFormatItem<FiberIdFormatItem>},     // 协程ID
        {'N' , &CreateSimpleFormatItem<ThreadNameFormatItem>}   // 大N线程名
    };
    return FORMAT_ITEM_FACTORIES;
}
//directive : 指示, 指令
//directive 为 d 时 ， format 是必须的 ， 其他时 format 非必须
std::unique_ptr<Formatter::FormatItem> CreateFormatItem(char directive , std::string_view format){
    const auto & factories = GetFormatItemFactories();
    const auto it = factories.find(directive);
    // 后面修改 ASSERT之后 ， 这里没找到时 会返回一个 nullptr 
    ASSERT_RETVAL(it != factories.end() , nullptr , "unknown logger format directive: " << directive);
    return it->second(format);
}


//插入纯文本内容 
// %d{%Y-%m-%d %H:%M:%S}.%u%Tthread=%t%Tfiber=%F%T[%p]%T%f:%l%T%m%n
void Formatter::addLiteral(std::string & literal){
    if(literal.empty()) return ;
    m_items.push_back(std::make_unique<LiteralFormatItem>(std::move(literal)));
    literal.clear();
}

int Formatter::parse(){
    std::string literal;

    for(std::size_t index = 0 ; index < m_pattern.size() ; ++index){
        //不是% 就当作 是要输出的普通字符
        if(m_pattern[index] != '%'){
            literal.push_back(m_pattern[index]);
            continue;
        }
        // 下面都是 m_pattern[index] 是 %
        
        // 以% 结尾了
        ASSERT_RETVAL( index +1 < m_pattern.size() , -1 , "logger format pattern ends with an incomplete directive" );

        const char directive  = m_pattern[++index];
        if(directive == '%'){  // %%
            literal.push_back('%');
            continue;
        }
        addLiteral(literal);  // 每两个%_  %_ 之间都有可能有 literal

        std::string_view item_format;
        // {  ...   }
        //  %     d     {          %Y-%m-%d %H:%M:%S}.%u%Tthread=%t%Tfiber=%F%T[%p]%T%f:%l%T%m%n
        //       index  index + 1 
        // 只有 %d 后面接 {}
        if(index + 1 < m_pattern.size()  && m_pattern[index +1 ] == '{'){
            const std::size_t closing_brace = m_pattern.find('}' , index + 2);

            ASSERT_RETVAL(closing_brace != std::string::npos && closing_brace != (index +2 ) , -2 ,  "missing a closing brace or empty");

            item_format =  std::string_view(m_pattern).substr(index +2 , closing_brace - index -2 );
            index = closing_brace;
        }
        //%d 
        //这里只能处理 %d 之后没有{} 的, 和 %d{...}  
        //      %d 后无{} ， item_format 会是个空 ->采用默认的 
        //%d{} , 会进入 assert ， 中断了
        m_items.push_back(CreateFormatItem(directive , item_format));


    }
    addLiteral(literal); // 最后的 literal 

    for(const auto & item : m_items ){
        ASSERT_RETVAL(item != nullptr , -3 , "logger formatter contains a null format item");
    }
    return 0;
}





} // namespace ans::detail

/*notes: 
time_since_epoch() 就是 std::chrono::time_point 提供的成员函数。返回当前时间点与该时钟纪元（epoch）之间的时间间隔。
对于 C++20 的 std::chrono::system_clock，epoch 是：1970-01-01 00:00:00 UTC, 也就是 Unix 时间的起点。
返回类型 ： std::chrono::duration

struct tm {
    int tm_sec;    // 秒
    int tm_min;    // 分
    int tm_hour;   // 时
    int tm_mday;   // 日
    int tm_mon;    // 月，从 0 开始
    int tm_year;   // 年，从 1900 开始
    // 其他成员
};

*/