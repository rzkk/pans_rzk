#ifndef PANS_SRC_LOGGER_BUFFER_H
#define PANS_SRC_LOGGER_BUFFER_H

#include <algorithm>
#include <array>
#include <cstddef>      // std::size_t、std::ptrdiff_t
#include <cstring>
#include <streambuf>   // 标准输入输出的stream buf 接口
#include <string_view>
#include <vector>

//绝大多数的日志不会很长 
//自定义一个日志缓冲区，针对高频、小数据量的日志场景，在栈上分配日志缓冲区，遇超长日志，再在堆上分配内存
// 类似于 SSO 实现机制

namespace pans::detail
{
//INLINE_CAPACITY:  日志的 栈buffer 
template<std::size_t INLINE_CAPACITY>
class InlineBuffer{
public:
    void append( const char *    data , std::size_t size ){  // 将传入的数据放入 buffer 
        //1 空日志直接返回
        if(size == 0) return ; 
        //2 堆空间为空 -> 以前的全在栈上
        if(m_overfolw.empty()){
            /*2.1 以前的 + 这次的 ，栈还能放到栈上
                操作 ： 直接memcpy 拷贝
            */
            if(m_size + size <= INLINE_CAPACITY ){
                std::memcpy(m_line.data() + m_size ,data ,size );
                m_size += size;
                return ;
            }else{   // 2.2  以前的 + 这次的 ，栈放不下了 -> 开辟堆空间
                const std::size_t required_capacity = m_size + size; // 至少需要这么多
                m_overfolw.reserve(std::max(INLINE_CAPACITY * 2 , required_capacity));
                // ? 这里的m_overfolw.end() 应该于 m_overfolw.begin() 是一致的
                m_overfolw.insert(m_overfolw.end() , m_line.data() , m_line.data() + m_size);
            }   
        } 
        //到这里就说明 需要插入的地方是 堆上
        // 这次的插入 
        m_overfolw.insert(m_overfolw.end() , data , data + size);  // 利用vector的扩容
        m_size += size;
    }

    void append(char value){
        append(&value , 1);
    }
    void append(std::string_view  value){
        append(value.data(), value.size());
    }
    [[nodiscard]] const char * data()const noexcept {
        return m_overfolw.empty() ? m_line.data() : m_overfolw.data();
    }

    [[nodiscard]] std::size_t size()const noexcept{
        return m_size;
    }
    // string_view : 就是一个二元组 //std::string_view 只是借用日志数据。
    [[nodiscard]]std::string_view view()const noexcept{
        return {data() , m_size};
    }
private:
    std::array<char , INLINE_CAPACITY> m_line;
    std::vector<char>m_overfolw; // 长日志 在堆上的 buffer 
    std::size_t  m_size = 0; //日志长度   // 需要默认初始化 
};

// 自定义buffer 是为了替换默认的输入输出的 buffer 
// 继承std::streambuf是为了能够能够继续使用流式输入接口，兼容ostream生态 
// 为了能使用这些： std::ostream  std::hex , setw setprecision

template<std::size_t INLINE_CAPACITY>
class SmallStreamBuffer final : public std::streambuf{
public:
    explicit SmallStreamBuffer(InlineBuffer<INLINE_CAPACITY>& buffer) noexcept
        :m_buffer(buffer){}

//子类重写 , 重写 streambuf的方法
protected:  
    // << 会调用 xsputn 
    std::streamsize xsputn(const char * data , std::streamsize size) override{
        // size < 0 非法 
        // size ==0 , 没有字符串输入
        if(size <= 0) return 0; 
        m_buffer.append(data , static_cast<std::size_t>(size));
        return size;//返回 本次 xsputn() 成功写入了多少字符。
    }
    // 为了单字符输出 // 如果不重写这个接口 有可能造成流错误
    //通常      using char_type = char;
    //          using int_type = int;  char只有8位 ，表示字符 ，但还需额外表示 “没有字符”或“写入失败”
    //    traits_type::eof() ： -1  ， but 不同编译器 eof 值不同
    // eof()	获取 EOF 特殊值   ，  eq_int_type(a, b)	比较两个 int_type 值
    //not_eof(c)	返回一个不是 EOF 的值，  to_char_type(c)	把 int_type 转为字符类型

    int_type overflow(int_type character) override{
        ////如果这次收到的是 EOF 特殊值，就不要把它当作实际字符追加到缓冲区，而是返回非 EOF 的结果，表示本次操作成功。
        if(traits_type::eq_int_type(character , traits_type::eof())){
            return traits_type::not_eof(character); // 它保证返回一个不等于 EOF 的值。
        }
        //通过 traits_type::to_char_type() 转换，比直接写一个不加解释的整数转字符更符合标准流的类型设计。
        m_buffer.append(traits_type::to_char_type(character));
        return character;
    }


private: 
    InlineBuffer<INLINE_CAPACITY>& m_buffer; //引用 
};





} // namespace pans::detail

#endif