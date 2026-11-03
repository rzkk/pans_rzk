#include <pans/logger/log_level.h>

#include<algorithm>

#include<array>
#include<string>
#include<cctype>

// ## : 标记拼接操作符
// #：宏参数变为字符串字面量 ， 字符串化操作符  , 得到字符串字面量
//预处理器在 C++ 编译器正式分析类型之前就把代码替换好了。
namespace pans
{
    std::string_view LogLevel::ToString(Level level) noexcept{  // "UNKONWN" 是字符串常量 ， 具有 静态存储期
        switch (level)
        {
#define XX(name) case Level::LOG_LV_##name: return #name;
            XX(DEBUG)
            XX(INFO)
            XX(WARN)
            XX(ERROR)
            XX(FATAL)
            XX(OFF)
#undef XX  
//取消宏定义 ， 宏不受 C++ 花括号作用域限制。
        }
        return "UNKONWN";
    }


    LogLevel::Level LogLevel::FromString(std::string_view value) noexcept { 
        //1 先统一转成大写
        // 
        std::array<char , 6>normalized{};
        if(value.size() > normalized.size()){
            return Level::LOG_LV_OFF;
        }
        std::transform( value.begin() ,value.end() ,normalized.begin() ,[](unsigned char character){
            return static_cast<char>(std::toupper(character));
        }  );
        // std::transform(value.begin() ,value.end(), normalized.begin(),
        //            [](unsigned char c) -> char { 
        //                 return std::toupper(c); 
        // }); // 直接在返回值时 类型转换

        const std::string_view upper_value(normalized.data() , value.size()); // 使用实际长度

#define XX(name) if(upper_value == #name) return Level::LOG_LV_##name;
        XX(DEBUG)
        XX(INFO)
        XX(WARN)
        XX(ERROR)
        XX(FATAL)
        XX(OFF)
#undef XX  
        return Level::LOG_LV_OFF;
    }

} // namespace pans
