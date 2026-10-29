# 接口库  ， 不产生编译产物 ， 只传递编译选项  # 类似于配置包
add_library(pans_options INTERFACE)

target_compile_options(pans_options INTERFACE
    # GCC/Clang 通用选项
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
            -Wall             # 开启绝大多数常用警告（养成零警告好习惯
            -Wextra           # 额外警告（比 -Wall 更严格） #开启 -Wall 未包含的额外警告（如未使用的参数、符号比较等）  # 通常与 -Wall 配合使用
            -Wpedantic        # 严格要求符合 ISO C++ 标准，如果用了非标准的语法扩展就报警 ， 即不能使用 -std=gnu++20 
            -fno-strict-aliasing #禁用“严格别名优化”，防止强转指针类型时被编译器优化出诡异 Bug
    >
)

# target_compile_options(pans_options INTERFACE
#     $<$<AND:$<PLATFORM_ID:Linux>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:
#             -fPIC       # 编译位置无关代码 # 生成位置无关代码 (Position-Independent Code)，编译动态库(.so)或被动态库链接时必须开启
#     >
# )


target_link_options(pans_options INTERFACE
    $<$<AND:$<PLATFORM_ID:Linux>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>>:
            -rdynamic   # 栈回溯需要 # 将所有函数符号导出到动态符号表；程序崩溃打印调用栈（backtrace）时，才能看到函数名而不是一串十六进制地址
                        # coredump 时 进行栈回溯 ，使用
    >
)

# Debug 模式宏定义
target_compile_definitions(pans_options INTERFACE
    $<$<CONFIG:Debug>:PANS_DEBUG>
)
# 当构建类型为 Debug 时，追加以下调试编译参数：
target_compile_options(pans_options INTERFACE
    $<$<CONFIG:Debug>:-O0>      # 彻底关闭编译器优化，保证单步调试时代码执行顺序与源码完全一致
    $<$<CONFIG:Debug>:-g3>      # 生成最高级别的调试信息（比普通 -g 多包含了宏定义信息，可在 gdb 中打印宏）
    $<$<CONFIG:Debug>:-ggdb>    # 生成专为 GDB 调试器量身定制的丰富调试信息
)

target_compile_options(pans_options INTERFACE
    # --- Release 模式（纯发布模式） ---
    $<$<CONFIG:Release>:-DNDEBUG>  # 定义 NDEBUG 宏，自动禁用标准库中的 assert() 断言，提升性能
    $<$<CONFIG:Release>:-O2>        # 开启二级性能优化（工业界最稳妥的生产环境优化级别，兼顾速度与安全）
    $<$<CONFIG:Release>:-fno-omit-frame-pointer># 保留栈帧指针寄存器(rbp)；极其重要！方便线上用 perf/火焰图做性能分析
    
    # --- RelWithDebInfo 模式（带调试符号的发布模式，线上最常用） ---
    $<$<CONFIG:RelWithDebInfo>:-DNDEBUG># 同样禁用 assert()
    $<$<CONFIG:RelWithDebInfo>:-O2># 开启二级优化
    $<$<CONFIG:RelWithDebInfo>:-g>  # 保留基础调试符号，方便线上程序崩溃生成 core dump 时排查问题
    $<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer> # 保留栈帧指针
)

# 代码覆盖率
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE) 
    # 编译期插桩：让编译器在每个代码块织入统计指令（仅在 Debug 模式下生效）
    target_compile_options(pans_options INTERFACE
        $<$<CONFIG:Debug>:--coverage>
    )
    target_link_options(pans_options INTERFACE
        $<$<CONFIG:Debug>:--coverage>
    )
endif()