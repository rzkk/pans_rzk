#ifndef PANS_INCLUDE_PANS_MACROS_H
#define PANS_INCLUDE_PANS_MACROS_H
//路径 + _

#include<cassert>  // assert 
#include<iostream>
#include<cstdint> 

// 对外提供的接口 ： x 为false  : 进入断言
// unlikely : c++20 , 告诉编译器这个条件极大概率不成立
// #define PANS_ASSERT(x) \
//     if(!(x)) [[unlikely]]{ \
//         std::cerr<<__FILE__<< ":" << __LINE__  <<"  ASSERT FAILED : " << #x <<"\nStacktrace : to do \n " ;\
//         assert(x); \
//     }

//带说明文字的assert  , 带有info参数
#define PANS_ASSERT(x , w) \
    if(!(x)) [[unlikely]]{ \
        std::cerr<< __FILE__ << ":"<< __LINE__ <<"  ASSERT " << #x <<" failed . [ " << w  <<"]. \nStacktrace : to do \n " ;\
        assert(x); \
    }

//函数中的assert ,带有返回值的
#define ASSERT_RETVAL(x , val,info)\
    do{ \
        if(x)[[likely]] break;\
        PANS_ASSERT(x,info );\
        return val;\
    }while(0)

//函数中的assert ,无返回值的
#define ASSERT_RETNONE(x ,info)\
    do{ \
        if(x)[[likely]] break;\
        PANS_ASSERT(x,info );\
        return ;\
    }while(0)

//函数中的assert 后不return  // 仅仅只是想出现这种情况提示一下下
#define ASSERT_NOEFFECT(x ,info)\
    do{ \
        if(x)[[likely]] break;\
        PANS_ASSERT(x,info );\
    } while(0)

// 循环中的 assert 
//x true : 正常
#define ASSERT_CONTINUE(x , info) \
    if(!(x))  [ [unlikely] ] {\
        PANS_ASSERT(x , info); \
        continue; \
    } else {}

#define ASSERT_BREAK(x , info) \
    if(!(x))  [ [unlikely] ] {\
        PANS_ASSERT(x , info); \
        break; \
    } else {}  


// 定义非法值 
#define INVALID64 (~0ULL)
#define INVALID32 0xFFFFFFFF
#define INVALID16 0xFFFF
#define INVALID8 0xFF

//定义 最大值
#define MAX_U8 0xFF
#define MAX_U16 0xFFFF
#define MAX_U32 0xFFFFFFFF
#define MAX_U64 (~0ULL)

using u8    = std::uint8_t;
using s8    = std::int8_t;
using u16   = std::uint16_t;
using s16   = std::int16_t;
using u32   = std::uint32_t;
using s32   = std::int32_t;
using u64   = std::uint64_t;
using s64   = std::int64_t;

#endif