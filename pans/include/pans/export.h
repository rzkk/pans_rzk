// 控制跨平台动态符号的导入导出
#ifndef PANS_INCLUDE_PANS_EXPORT_H
#define PANS_INCLUDE_PANS_EXPORT_H

// 当前目标平台为 Windows + PANS_SHARED_LIBRARY：工程声明当前使用动态库模式。
// PANS_SHARED_LIBRARY 是你自己约定的宏，不会因为 CMake 创建了动态库就自动定义。
#if defined(_WIN32) && defined( PANS_SHARED_LIBRARY )

    // 在编译pans库本身的时候，通过__declspec(dllexport)，告诉编译器，把符号导出到DLL
    #if defined(PANS_BUILDING_LIBRARY)
        #define PANS_API __declspec(dllexport)
    #else 
    // 其它项目使用pans动态库的时候，__declspec(dllimport)告诉编译器，符号来自于其他DLL
        #define PANS_API __declspec(dllimport)
    #endif
    
// 通过__attribute__((visibility("default")))，确保符号对外可见
#elif defined(__GNUC__) && defined(PANS_SHARED_LIBRARY)
    #define PANS_API __attribute__( ( visibility("default") ) ) //该符号具有默认可见性，可以作为共享库对外可见的符号。
#else 
    #define PANS_API
#endif 

#endif 


/*notes 
静态库与动态库在“是否需要导入导出”上的根本区别，源于它们参与链接的时机以及最终二进制文件的边界完全不同。
简而言之：静态库在编译期被直接融进主程序，属于“内部消化”；而动态库在运行期才独立加载，属于“跨边界调用”。
1. 静态库（Static Library）：编译期的“复制粘贴”
静态库（Windows 下的 .lib，Linux 下的 .a）本质上只是一个把多个编译好的目标文件（.obj 或 .o）打包在一起的压缩包（Archive），它本身并不是一个完整的独立程序。
    1.1无边界融合： 当你的主程序链接静态库时，链接器（Linker）会直接从静态库中把用到的函数代码完整地复制并合并到最终的可执行文件（.exe）中。
    1.2无需公开表： 合并完成后，静态库里的函数和你自己写的 main.cpp 里的函数处于同一个可执行文件内部。
        链接器在编译阶段就能直接算出所有函数的内存相对地址，既不存在外部调用，也不存在模块边界，自然不需要任何“导入”或“导出”标记。
 2. 动态库（Shared/Dynamic Library）：运行期的“跨文件寻址”
动态库（.dll / .so / .dylib）是一个独立编译、独立存在的二进制文件。它的代码不会被复制到主程序中，而是在主程序运行起来之后，由操作系统的动态加载器临时加载到内存里。
因为它是独立模块，必须通过“导出”和“导入”两个动作来打通跨模块的调用：
    2.1 为什么要“导出”（Export）—— 制作对外菜单： 一个动态库内部可能有上千个函数，但其中 90% 都是内部辅助函数，只有 10% 是提供给外部使用的接口。
        动态库在编译时，必须生成一张导出符号表（Export Table），相当于餐厅的“对外菜单”。

        封装与防冲突： 只把标记了导出的函数写进这张表，隐藏内部实现，防止多个动态库之间出现同名内部函数冲突（符号污染）。

        体积与性能： 导出表越小，动态库文件体积越小，操作系统在启动程序时查找和绑定函数地址的速度就越快。

    2.2 为什么要“导入”（Import）—— 生成跨模块跳板：
        当主程序调用动态库里的函数时，编译器在编译主程序的那一刻，根本不知道这个函数将来在内存的哪个位置。

        导入表（Import Table）： 标记为导入（如 Windows 的 __declspec(dllimport)）后，编译器知道这是一个“外部函数”，不会去报错说找不到函数体，
        而是会在主程序里留出一个空位（导入地址表 IAT）。等程序启动时，操作系统会读取动态库的“导出表”，把真实内存地址填进主程序的“导入表”里。

        指令级优化（Windows 特有）： 在 Windows 下，如果不写 dllimport，编译器会默认按普通内部函数生成调用指令，链接器随后不得不生成一个额外的跳转指令（Thunk/Stub）来中转；
        而显式声明 dllimport 后，编译器会直接生成读取导入表指针的指令，省去一次汇编级别的跳转，提升运行效率。

另外： 在编译动态链接库（Windows 下的 .dll，Linux 下的 .so，macOS 下的 .dylib）时，不同平台对“哪些函数或类可以被外部程序调用”有着完全不同的默认规则：

    Windows (MSVC 等)： 默认隐藏所有符号。你必须显式用 __declspec(dllexport) 标记要导出的内容；
        而在外部程序调用该 DLL 时，最好用 __declspec(dllimport) 标记以优化调用性能。

    Linux / macOS (GCC / Clang)： 默认导出所有符号。但在大型项目中，为了减小动态库体积、加快加载速度并防止符号冲突，通常会加上编译参数 -fvisibility=hidden 把默认行为改成“
        全部隐藏”，然后只对公开的 API 显式标记 __attribute__((visibility("default")))。

    静态库 (Static Library)： 不存在动态导入导出的概念，不需要任何特殊修饰符。

    为了不在每个函数前都写一堆 #ifdef，开发者用 PANS_API 把这些差异全部屏蔽掉
*/