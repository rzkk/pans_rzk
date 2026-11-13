# 本文件的目标：把 third_party 中的源码分别配置、编译并安装到一个缓存目录，
# 再让 pans 使用这些安装结果。它刻意不使用 add_subdirectory(third_party/xxx)，
# 这样第三方库的选项、目标名和编译设置不会污染 pans 自己的 CMake 工程。
#
# 阅读时先区分 CMake 的两个阶段：
# 1. 配置阶段：执行本文件中的 set()/if()/function()/add_custom_target() 等命令，
#    生成底层构建系统（例如 Makefile 或 Ninja 文件）。
# 2. 构建阶段：真正执行 add_custom_command() 中登记的“配置第三方库、编译、安装”命令。
# execute_process() 是例外，它会在配置阶段立即执行。
#
# 当前工程状态提醒：顶层 CMakeLists.txt 需要 include() 本文件，本逻辑才会生效；
# build_commands 还要求 cmake/writeDependencyStamp.cmake 存在，kcp 包装构建还要求
# cmake/kcp/CMakeLists.txt 存在。缺少这些接入条件时，本文件的设计尚不能完整运行。



# 第三方依赖的源码根目录。
#set(变量名 变量值 CACHE 缓存类型 "描述信息")
#   ${PROJECT_SOURCE_DIR} ： 当前 project() 对应的项目源码根目录
#使用 CACHE 变量后，可以在 CMake 配置时通过,CACHE PATH 表示它会写入canche缓存 CMakeCache.txt 中，
#用户可以在首次配置时用 -DPANS_THIRD_PARTY_DIR=/some/path 覆盖默认值，之后会沿用缓存值。
#普通变量用于脚本内部计算；缓存变量适合需要由用户配置、并且在多次配置间保留的选项
#几个常见的缓存变量类型：类型	        用途
#                       BOOL	    开关
#                       PATH	    目录路径
#                       FILEPATH	文件路径
#                       STRING	    普通字符串
set(PANS_THIRD_PARTY_DIR "${PROJECT_SOURCE_DIR}/third_party" CACHE PATH "third-party source")
# 每个依赖最终执行 cmake --install 的根目录；这里也保存构建目录和缓存标记。
#PANS_THIRD_PARTY_OUTPUT_DIR :编译产物根目录，默认 third_party/output
set(PANS_THIRD_PARTY_OUTPUT_DIR "${PANS_THIRD_PARTY_DIR}/output" CACHE PATH "compiled third-party output")
set(PANS_THIRD_PARTY_BUILD_TYPE Release CACHE STRING "third-party build-type")

# 为缓存变量指定了建议的可选值，方便在 CMake GUI 中选择
set_property(CACHE PANS_THIRD_PARTY_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)

#语法 ：  option(变量名 "说明" 默认值)
# 可以使用 -DPANS_UPDATE_SUBMODULES=OFF 覆盖默认值，进行关闭。
#含义：  是否在依赖缺失时初始化或更新 Git 子模块。 源码缺失时，是否尝试通过 Git 初始化子模块
option(PANS_UPDATE_SUBMODULES "Initialize missing third-party git submodules" ON)

# 是否优先复用 third_party/output 中与当前配置兼容的已编译依赖。 是否复用已编译产物
# 关闭后通常会强制走第三方依赖的重新构建流程。
option(PANS_REUSE_INSTALLED_DEPENDENCIES "Reuse compatible dependencies from third_party/output" ON)

#流程:foreach 拆分 → 检查哨兵文件 → 收集缺失项 → 若缺失则 git submodule update --init --recursive -- <paths> → 再校验一次。
# CMake 没有真正独立的数组类型，通常使用分号分隔的字符串表示列表：子模块完整性检查清单。
# 每一项使用“子模块目录|哨兵文件”格式：当哨兵文件不存在时，说明对应子模块可能未初始化或内容不完整，后续配置逻辑可据此决定是否同步子模块。
# 这里选择稳定且能代表源码已检出的文件，而不是依赖目录本身是否存在。
set(_pans_required_submodules
    "third_party/yaml-cpp|CMakeLists.txt"
    "third_party/abseil-cpp|CMakeLists.txt"
    "third_party/protobuf|CMakeLists.txt"
    "third_party/rapidjson|include/rapidjson/document.h"
    "third_party/tinyxml2|CMakeLists.txt"
    "third_party/kcp|ikcp.c"                
    "third_party/zlib|CMakeLists.txt"
)

# 查找 Git。只有在需要初始化或更新子模块时，后续逻辑才需要使用它；
# 得到Git_FOUND ， GIT_EXECUTABLE
# QUIET 表示查找失败时不要输出普通提示。
find_package(Git QUIET)

# 函数格式 ： 
#     function(function_name parameter1 parameter2)
#     ...
#       endfunction()

# notes: list 构建
# 方式 1：多元素直接定义（底层等价于 "item1;item2;item3"）
# set(_pans_required_submodules
#     "third_party/yaml-cpp|CMakeLists.txt"
#     "third_party/abseil-cpp|CMakeLists.txt"
# )

# # 方式 2：通过 string(REPLACE) 将竖线替换为分号，直接生成含 2 个元素的列表 parts
#   string(REPLACE "|" ";" parts "${entry}") 
function(pans_init_third_party_submodules)
    # function() 会创建局部变量作用域；这里不会污染调用者的 missing_submodules。
    set(missing_submodules)  #刚开始为空
    # 收集哪些子模块缺失或内容不完整
    # IN LISTS 后面写的是“变量名”，不是 ${变量值}。
    foreach(entry IN LISTS _pans_required_submodules)
        #语法：string(REPLACE 原字符串 替换字符串 结果变量 输入字符串)
        # "a|b" -> "a;b"；分号让 parts 成为含两个元素的 CMake 列表。
        string(REPLACE "|" ";" parts "${entry}")  # 把字符串中的 | 替换成 ; 
        #list(GET 列表 下标 输出变量) ，下标从 0 开始。
        list(GET parts 0 submodule_path)
        list(GET parts 1 sentinel)
        # 如果路径不存在 // if(VAR) if(NOT VAR)  if(VAR STREQUAL "Linux") if(EXISTS "/tmp/file") if(TARGET some_target)
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${submodule_path}/${sentinel}")
            list(APPEND missing_submodules "${submodule_path}")
        endif()
    endforeach()
    # 如果不存在缺失的子模块，则提示用户并返回
    if(NOT missing_submodules)
        return()   #这里的 return() 只结束当前函数
    endif()

    # 如果存在缺失的子模块，则提示用户并尝试初始化或更新子模块
    if(NOT PANS_UPDATE_SUBMODULES)  # 如果没有打开子模块更新标记
        message(FATAL_ERROR
            "Missing third-party source trees: ${missing_submodules}\n"
            "Enable PANS_UPDATE_SUBMODULES or run:\n"
            "  git submodule update --init --recursive")
    endif()

    # 网站压缩包下载的 就没有这个文件夹 ， 不能更新
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/.git")
        message(FATAL_ERROR
            "Missing third-party source trees: ${missing_submodules}\n"
            "The source tree is not a git checkout, so CMake cannot initialize them.")
    endif()

    if(NOT Git_FOUND)  # 没有找到git 工具
        message(FATAL_ERROR "Git is required to initialize missing submodules.")
    endif()

    message(STATUS "Initializing missing git submodules: ${missing_submodules}")
    #在 CMake 配置阶段执行外部程序。等价于： 
    # git submodule update --init --recursive -- third_party/yaml-cpp  third_party/protobuf
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" submodule update --init --recursive -- ${missing_submodules}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"  #命令执行目录
        RESULT_VARIABLE submodule_result          # 保存进程退出码     0     成功 ， 非 0  失败
        OUTPUT_VARIABLE submodule_output          # 捕获标准输出
        ERROR_VARIABLE submodule_error            # 捕获标准错误
    )

    if(NOT submodule_result EQUAL 0)#接着检查退出码
        #message(FATAL_ERROR "...")  会立即终止 CMake 配置。
        message(FATAL_ERROR    
            "git submodule update failed (exit=${submodule_result}):\n"
            "${submodule_output}${submodule_error}")
    endif()
    #再次检查
    foreach(entry IN LISTS _pans_required_submodules)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 submodule_path)
        list(GET parts 1 sentinel)
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${submodule_path}/${sentinel}")
            message(FATAL_ERROR
                "Required third-party source is still missing: "
                "${submodule_path}/${sentinel}")
        endif()
    endforeach()

endfunction()

pans_init_third_party_submodules() # 执行一下函数


#如何知道之前编译过的二进制库，还能不能继续使用？ 最基本的条件是：源码版本没有变化。
# 获取子模块commit ID的函数 -> 用于判断子模块是否被更新过
# 取得依赖源码版本，作为后面缓存键的一部分。源码版本变化时，旧二进制缓存必须失效。
function(pans_get_source_revision source_dir output_variable)
     # Git 仓库（包括 .git 为文件的子模块）使用精确 commit ID。
    if(Git_FOUND AND EXISTS "${source_dir}/.git") #有 git 时用 commit SHA 作为版本标识。
        execute_process(
            #git -C third_party/yaml-cpp rev-parse HEAD 
            # -C：让 Git 在指定目录下操作。 
            # rev-parse HEAD：取得当前 HEAD 指向的提交 ID。
            COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse HEAD
            RESULT_VARIABLE revision_result
            OUTPUT_VARIABLE revision
            ERROR_VARIABLE revision_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT revision_result EQUAL 0)
            message(FATAL_ERROR "Failed to read third-party revision in ${source_dir}: ${revision_error}")
        endif()
    else()
        #计算指定文件内容的 SHA256 哈希值。 当文件内容改变时，哈希值通常也会发生改变
        file(SHA256 "${source_dir}/CMakeLists.txt" revision) #无 git 时退化为 CMakeLists.txt 的 SHA256。
    endif()
    set(${output_variable} "${revision}" PARENT_SCOPE)
    # PARENT_SCOPE ： 将变量设置到父作用域，而不是当前函数作用域。 
endfunction()



#给它一个第三方库的信息，它就自动帮你创建配置、编译、安装、缓存和依赖管理任务。
function(pans_add_cmake_dependency)
    # 解析函数的命名参数 ， 
    # cmake_parse_arguments() 主要处理三种关键字。三组参数依次是：布尔选项、单值参数、多值参数。
        # 类型	        含义	                   本例
        # Option	    不需要额外值的开关	        没有
        # One-value	    后面跟一个值	        NAME、SOURCE_DIR、SOURCE_REVISION
        # Multi-value	后面可以跟多个值	CMAKE_ARGS、SENTINELS、DEPENDS、KEY_MATERIAL
        # PARSE_ARGV    0 表示从本函数的第 0 个实参开始解析，并把结果放进 dep_ 前缀变量。
    # 如：
            # pans_add_cmake_dependency(         # 调用的时候 的样子
            # NAME yaml-cpp
            # SOURCE_DIR "/tmp/yaml-cpp"
            # SOURCE_REVISION "abc123"         # 用的是 git的 commit ID

            # CMAKE_ARGS
            #     "-DBUILD_SHARED_LIBS=OFF"
            #     "-DYAML_CPP_BUILD_TESTS=OFF"

            # SENTINELS
            #     "include/yaml-cpp/yaml.h"
            #     "lib/libyaml-cpp.a"
            # ) dep_NAME
            #解析后自动生成：     # dep_NAME = yaml-cpp
                                # dep_SOURCE_DIR = /tmp/yaml-cpp
                                # dep_SOURCE_REVISION = abc123
                                # dep_CMAKE_ARGS =
                                #     -DBUILD_SHARED_LIBS=OFF
                                #     -DYAML_CPP_BUILD_TESTS=OFF
                                # dep_SENTINELS =           #sentinels：哨兵；岗哨；守卫者
                                #     include/yaml-cpp/yaml.h
                                #     lib/libyaml-cpp.a
    cmake_parse_arguments(PARSE_ARGV 0 dep ""
        "NAME;SOURCE_DIR;SOURCE_REVISION"
        "CMAKE_ARGS;SENTINELS;DEPENDS;KEY_MATERIAL")

    if(NOT dep_NAME OR NOT dep_SOURCE_DIR OR NOT dep_SOURCE_REVISION)
        message(FATAL_ERROR "pans_add_cmake_dependency requires NAME, SOURCE_DIR and SOURCE_REVISION")
    endif()

    if(NOT dep_SENTINELS)
        message(FATAL_ERROR "pans_add_cmake_dependency(${dep_NAME}) requires SENTINELS")
    endif()

    # notes :
        # 假设你之前用：GCC 13 + Linux + x86_64 + Release
        # 现在切换成 ：Clang 18 + Linux + x86_64 + Debug   ， 就不能再复用以前的静态库
        # CONCAT : 将多个变量连接成一个字符串 , 只是拼接字符串，不会自动添加分隔符。
        # SHA256: 计算字符串的 SHA256 哈希值
        
    
    string(CONCAT cache_material
        "${dep_SOURCE_REVISION};${CMAKE_C_COMPILER};${CMAKE_C_COMPILER_ID};"
        "${CMAKE_C_COMPILER_VERSION};${CMAKE_CXX_COMPILER};${CMAKE_CXX_COMPILER_ID};"
        "${CMAKE_CXX_COMPILER_VERSION};${CMAKE_SYSTEM_NAME};${CMAKE_SYSTEM_PROCESSOR};"
        "${CMAKE_GENERATOR};${CMAKE_GENERATOR_PLATFORM};${CMAKE_GENERATOR_TOOLSET};"
        "${CMAKE_OSX_ARCHITECTURES};${CMAKE_OSX_DEPLOYMENT_TARGET};"
        "${CMAKE_MSVC_RUNTIME_LIBRARY};${PANS_THIRD_PARTY_BUILD_TYPE};static;"
        "${dep_CMAKE_ARGS};${dep_KEY_MATERIAL}")

    # 以上配置变化都会触发新的编译，不会误用前一个结果，从而保证第三方库的  ABI一致

    string(SHA256 cache_key "${cache_material}")
    #   string(SUBSTRING <string> <begin> <length> <output_variable>)

    string(SUBSTRING "${cache_key}" 0 16 cache_key_short) # SUBSTRING : 从字符串中提取子字符串    

    # PANS_THIRD_PARTY_OUTPUT_DIR:  ${PANS_THIRD_PARTY_DIR}/output
    # 不同构建参数 有不同的版本
    # /project/third_party/output/
    # ├── build/
    # │   └── yaml-cpp-1234567890abcdef/
    # │       ├── CMakeCache.txt
    # │       ├── build.ninja
    # │       └── 其他编译中间产物
    # │
    # └── yaml-cpp/
    #     └── 1234567890abcdef/
    #         ├── include/
    #         ├── lib/
    #         └── .pans-cache-完整SHA256值
    set(install_dir "${PANS_THIRD_PARTY_OUTPUT_DIR}/${dep_NAME}/${cache_key_short}") #保存最终安装的头文件、静态库、CMake 配置文件 以cache_key 记录版本
    set(build_dir "${PANS_THIRD_PARTY_OUTPUT_DIR}/build/${dep_NAME}-${cache_key_short}") #放中间产物、CMakeCache.txt、构建规则
    set(cache_stamp "${install_dir}/.pans-cache-${cache_key}") # pans-cache-${cache_key}: 标记版本   # stamp : 邮票印记  # 


    # 只有全部哨兵文件和缓存标记都存在，才认为缓存完整。
    set(installed_outputs "${cache_stamp}")  # installed_outputs 是一组文件路径，稍后会传给 add_custom_command(OUTPUT ...)，让底层构建系统追踪这些文件
    set(cache_complete TRUE) #cache_complete 是配置阶段计算出的布尔变量，用于当前脚本中的判断和状态提示。
    foreach(sentinel IN LISTS dep_SENTINELS)
        if(IS_ABSOLUTE "${sentinel}")  # IS_ABSOLUTE :   用于判断给定的路径字符串是否为绝对路径
            set(sentinel_path "${sentinel}")
        else()
            set(sentinel_path "${install_dir}/${sentinel}")
        endif()
        # 向末尾追加元素 list(APPEND)
        # list(APPEND <列表变量名> <元素1> [<元素2> ...])
        list(APPEND installed_outputs "${sentinel_path}")
        if(NOT EXISTS "${sentinel_path}")  #如果文件不存在
            set(cache_complete FALSE)
        endif()
    endforeach()

    if(NOT EXISTS "${cache_stamp}")  # 只有全部关键输出文件和缓存标记都存在，才保持为 TRUE。
        set(cache_complete FALSE)
    endif()

    # 传递编译器和工具链 ， 希望第三方库尽量使用与主工程一致的编译器。
    #  CMAKE_COMMAND: CMake 的可执行文件路径   如：有可能是 /usr/bin/cmake
    #选项	作用
    # -S	指定源码目录，CMake 会从这里查找 CMakeLists.txt
    # -B	指定构建目录，用于生成 CMakeCache.txt 和构建文件
    # -G	指定生成器，例如 Ninja、Unix Makefiles
    # -A：选择支持该选项的生成器所使用的目标平台，例如 Visual Studio 的 x64。
    # -T：选择支持该选项的生成器所使用的工具集，例如 Visual Studio 的 v143。

    set(configure_command "${CMAKE_COMMAND}" -S "${dep_SOURCE_DIR}" -B "${build_dir}" -G "${CMAKE_GENERATOR}")

    # 然后把当前项目使用的目标平台和编译工具集传递给第三方库，保持一致
    if(CMAKE_GENERATOR_PLATFORM)  # 先判断是否有值
        list(APPEND configure_command -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND configure_command -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    
    # "-DCMAKE_INSTALL_LIBDIR=lib" ： 告诉遵循 GNUInstallDirs 等约定的第三方工程，库文件安装目录使用 lib
    #                                   但该变量是否实际生效，取决于第三方工程是否使用了它。
    #  "-DCMAKE_POSITION_INDEPENDENT_CODE=ON" ：要求相关编译目标默认使用位置无关代码。
    # BUILD_TESTING ：用于关闭支持该标准测试选项的第三方工程的测试构建。
    list(APPEND configure_command
        "-DCMAKE_INSTALL_PREFIX=${install_dir}"    #指定安装的根目录
        "-DCMAKE_INSTALL_LIBDIR=lib"
        "-DCMAKE_BUILD_TYPE=${PANS_THIRD_PARTY_BUILD_TYPE}"
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
        "-DBUILD_TESTING=OFF"
    )
    
    if(CMAKE_C_COMPILER)
        list(APPEND configure_command "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
    endif()
    if(CMAKE_CXX_COMPILER)
        list(APPEND configure_command "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND configure_command "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(CMAKE_MSVC_RUNTIME_LIBRARY)
        list(APPEND configure_command "-DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}")
    endif()
    if(CMAKE_OSX_ARCHITECTURES)
        list(APPEND configure_command "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        list(APPEND configure_command "-DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    list(APPEND configure_command ${dep_CMAKE_ARGS}) # 把调用者传入的、每个第三方库特有的配置选项追加进去。
    # 把完整构建流程保存到build_commands裏面，一共有四步, 分别是cmake, 编译，安装，写入缓存标记

    # COMMAND ${configure_command} ：  
    # 展开后类似：cmake \
                    # -S third_party/yaml-cpp \
                    # -B third_party/output/build/yaml-cpp-1234567890abcdef \
                    # -G Ninja \
                    # -DCMAKE_INSTALL_PREFIX=... \
                    # -DCMAKE_BUILD_TYPE=Release \
                    # -DBUILD_SHARED_LIBS=OFF
    # 这里实际上是在 pans 的构建任务中，启动了另一个独立的 CMake 配置过程。
    # 2 ：  cmake --build build/yaml-cpp --config Release --parallel 8
    # CMAKE_BUILD_PARALLEL_LEVEL 在 CMake 中主要是一个环境变量 ， 用于控制并行编译的线程数。  该变量定义在顶层 cmakelists 
    # cmake --install build/yaml-cpp --config Release   安装第三方库 
    # COMMAND "${CMAKE_COMMAND}" "-DPANS_DEPS_STAMP_FILE=${cache_stamp}" "-DPANS_DEPS_STAMP_VALUE=${cache_key}" -P "${PROJECT_SOURCE_DIR}/cmake/writeDependencyStamp.cmake"
        # -P 表示以脚本模式执行 .cmake 文件    ， 
        #等于：
        # 1. 向脚本传递两个变量。
        # 2. 执行对应的 .cmake 脚本。
        # 3. 由脚本完成缓存标记文件的写入
    set(build_commands
        COMMAND ${configure_command}
        COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --config "${PANS_THIRD_PARTY_BUILD_TYPE}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL}"
        COMMAND "${CMAKE_COMMAND}" --install "${build_dir}" --config "${PANS_THIRD_PARTY_BUILD_TYPE}"
        COMMAND "${CMAKE_COMMAND}" "-DPANS_DEPS_STAMP_FILE=${cache_stamp}" "-DPANS_DEPS_STAMP_VALUE=${cache_key}" -P "${PROJECT_SOURCE_DIR}/cmake/writeDependencyStamp.cmake"
    )
    # 多个 COMMAND 会按顺序执行  ，但是若第一条命令里的 Shell 环境变量修改，不会自动保留到第二条命令 即认为它们一定是在同一个持续存在的 Shell 会话里执行。


    # 先生成目标名称 ： 若dep_NAME = yaml-cpp  ， 那么ready_target = pans_dep_yaml-cpp_ready
    set(ready_target "pans_dep_${dep_NAME}_ready")  
    # 将 - 替换为 _。 pans_dep_yaml_cpp_ready
    string(REPLACE "-" "_" ready_target "${ready_target}")
    
    # “这些 OUTPUT 文件是由这些 COMMAND 生成的。如果有人需要这些文件，就执行这些命令。
    # notes： add_custom_command(                     # 告诉构建系统，如何生成某些指定的文件。
            #     OUTPUT  <输出文件...>                 # 这些文件是“产物”
            #     COMMAND <要执行的命令及参数...>       # 第一条命令
            #     DEPENDS <输入依赖...>
            #     COMMENT <显示的信息>
            #     VERBATIM
            # )
    # 如 ：add_custom_command(
            #     OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hello.txt"
            #     COMMAND "${CMAKE_COMMAND}" -E touch  "${CMAKE_CURRENT_BINARY_DIR}/hello.txt"
            #     COMMENT "Generating hello.txt"
            #     VERBATIM
            # )
    # 构建目标
        # pans_dep_yaml_cpp_ready
        #          |
        #          | DEPENDS
        #          v
        # 要求输出文件存在
        # libyaml-cpp.a
        # yaml.h
        # .pans-cache-...
        #          |
        #          | 对应的生成规则
        #          v
        # add_custom_command()
        #          |
        #          v
        # 配置 → 编译 → 安装 → 写缓存标记
    #   installed_outputs 里包含 ： 所有哨兵文件（如 include/yaml-cpp/yaml.h、静态库文件）， 加上一个 stamp 文件（${cache_stamp}）
    #   add_custom_command 告诉 CMake：这些文件是由上面那 4 条命令依次生成的 ，  如果这些文件已经存在且是最新的，就不会重新执行命令。
     # 优点：如果哨兵文件和 stamp 都存在，就完全跳过构建。特点：真正实现了“缓存命中时零开销”。
     # 注意：
        # OUTPUT 文件必须被某个目标依赖，否则命令永远不会执行。
        # 路径最好用绝对路径，避免工作目录问题。
        # 多条 COMMAND 是顺序执行的，前面失败后面不会继续。
        # add_custom_command 不能直接依赖其他目标（只能依赖文件），所以本文件用 add_dependencies 来建立第三方库之间的顺序。
        # stamp 文件的作用：即使所有哨兵文件都存在，只要 stamp 不存在或内容不匹配，也会重新构建。这是一种比单纯依赖文件时间戳更可靠的机制
    if(PANS_REUSE_INSTALLED_DEPENDENCIES)
        add_custom_command(
            OUTPUT ${installed_outputs}
            ${build_commands}
            COMMENT "Building and installing third-party dependency: ${dep_NAME}"
            VERBATIM                                                    # 让 CMake 对命令参数做正确的转义，避免 shell 解释特殊字符（空格、引号、$ 等）。
            COMMAND_EXPAND_LISTS                                        # 允许把 CMake 列表正确展开成多个参数。
        )
        # add_custom_target() 负责创建一个能够让开发者或其他目标引用的构建目标。
        # add_custom_target 的作用创建一个名为 pans_dep_yaml-cpp_ready 的目标。这个目标依赖于上面那些 OUTPUT 文件。当主工程或其他依赖需要这个目标时，就会触发检查，必要时执行构建。
        add_custom_target("${ready_target}" DEPENDS ${installed_outputs})
    else()
        add_custom_target("${ready_target}"
            ${build_commands}
            COMMENT "Rebuilding third-party dependency: ${dep_NAME}"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )
    endif()

    # 添加第三方库的依赖，用于建立第三方库之间的构建顺序
    # add_dependencies(
    #                       <目标>
    #                   <依赖目标1>
    #                   <依赖目标2>
    #                       ...
    #                   )
    # protobuf 需要这种机制 ：
    if(dep_DEPENDS)
        add_dependencies("${ready_target}" ${dep_DEPENDS})
    endif()
    
    if(cache_complete AND PANS_REUSE_INSTALLED_DEPENDENCIES)
        message(STATUS "${dep_NAME}: reusing ${install_dir}")
    else()
        message(STATUS "${dep_NAME}: build scheduled; output ${install_dir}")
    endif()
    
    # string(TOUPPER <输入字符串> <输出变量>) ： 将字符串转换为大写。 
    string(TOUPPER "${dep_NAME}" variable_prefix)
    string(REPLACE "-" "_" variable_prefix "${variable_prefix}")
    # PARENT_SCOPE ：把变量设置到当前函数的直接父作用域。 
    set("PANS_${variable_prefix}_INSTALL_DIR" "${install_dir}" PARENT_SCOPE)
    set("PANS_${variable_prefix}_CACHE_KEY" "${cache_key}" PARENT_SCOPE)
    set("PANS_${variable_prefix}_CACHE_COMPLETE" "${cache_complete}" PARENT_SCOPE)
    set("PANS_${variable_prefix}_DEPENDENCY_TARGET" "${ready_target}" PARENT_SCOPE)

endfunction()



# 统一的静态库文件名模板
set(_pans_static_library "${CMAKE_STATIC_LIBRARY_PREFIX}@NAME@${CMAKE_STATIC_LIBRARY_SUFFIX}")

# -------- abseil-cpp --------
set(_pans_absl_source "${PANS_THIRD_PARTY_DIR}/abseil-cpp")
pans_get_source_revision("${_pans_absl_source}" _pans_absl_revision)
string(REPLACE "@NAME@" "absl_base" _pans_absl_library "${_pans_static_library}")
pans_add_cmake_dependency(
    NAME abseil-cpp
    SOURCE_DIR "${_pans_absl_source}"
    SOURCE_REVISION "${_pans_absl_revision}"
    CMAKE_ARGS
        "-DBUILD_SHARED_LIBS=OFF"
        "-DABSL_ENABLE_INSTALL=ON"
        "-DABSL_PROPAGATE_CXX_STD=ON"
        "-DABSL_BUILD_TESTING=OFF"
    SENTINELS
        "lib/cmake/absl/abslConfig.cmake"
        "include/absl/base/config.h"
        "lib/${_pans_absl_library}"
)

# -------- zlib --------
set(_pans_zlib_source "${PANS_THIRD_PARTY_DIR}/zlib")
pans_get_source_revision("${_pans_zlib_source}" _pans_zlib_revision)
string(REPLACE "@NAME@" "z" _pans_zlib_library "${_pans_static_library}")
if(MSVC)
    string(REPLACE "@NAME@" "zlibstatic" _pans_zlib_library "${_pans_static_library}")
endif()
pans_add_cmake_dependency(
    NAME zlib
    SOURCE_DIR "${_pans_zlib_source}"
    SOURCE_REVISION "${_pans_zlib_revision}"
    CMAKE_ARGS
        "-DZLIB_BUILD_TESTING=OFF"
        "-DZLIB_BUILD_SHARED=OFF"
        "-DZLIB_BUILD_STATIC=ON"
        "-DZLIB_INSTALL=ON"
    SENTINELS
        "lib/cmake/zlib/ZLIBConfig.cmake"
        "include/zlib.h"
        "lib/${_pans_zlib_library}"
)

# -------- yaml-cpp --------
# yaml-cpp 是一个 C++ 库，用于解析和生成 YAML 格式的文件。
# _pans_yaml_source : "${PANS_THIRD_PARTY_DIR}/yaml-cpp"  -> "rzkpans/third_party/yaml-cpp"   # yaml 的文件夹名
# _pans_yaml_revision ： yaml 的版本号
set(_pans_yaml_source "${PANS_THIRD_PARTY_DIR}/yaml-cpp")
pans_get_source_revision("${_pans_yaml_source}" _pans_yaml_revision)
string(REPLACE "@NAME@" "yaml-cpp" _pans_yaml_library "${_pans_static_library}")
pans_add_cmake_dependency(
    NAME yaml-cpp
    SOURCE_DIR "${_pans_yaml_source}"
    SOURCE_REVISION "${_pans_yaml_revision}"
    CMAKE_ARGS
        "-DBUILD_SHARED_LIBS=OFF"
        "-DYAML_BUILD_SHARED_LIBS=OFF"
        "-DYAML_CPP_BUILD_TESTS=OFF"
        "-DYAML_CPP_BUILD_TOOLS=OFF"
        "-DYAML_CPP_FORMAT_SOURCE=OFF"
        "-DYAML_CPP_INSTALL=ON"
        "-DYAML_CPP_DISABLE_UNINSTALL=ON"
    SENTINELS
        "lib/cmake/yaml-cpp/yaml-cpp-config.cmake"
        "include/yaml-cpp/yaml.h"
        "lib/${_pans_yaml_library}"
)

# -------- tinyxml2 --------
set(_pans_tinyxml2_source "${PANS_THIRD_PARTY_DIR}/tinyxml2")
pans_get_source_revision("${_pans_tinyxml2_source}" _pans_tinyxml2_revision)
string(REPLACE "@NAME@" "tinyxml2" _pans_tinyxml2_library "${_pans_static_library}")
pans_add_cmake_dependency(
    NAME tinyxml2
    SOURCE_DIR "${_pans_tinyxml2_source}"
    SOURCE_REVISION "${_pans_tinyxml2_revision}"
    CMAKE_ARGS
        "-DBUILD_SHARED_LIBS=OFF"
        "-Dtinyxml2_SHARED_LIBS=OFF"
        "-Dtinyxml2_BUILD_TESTING=OFF"
    SENTINELS
        "lib/cmake/tinyxml2/tinyxml2-config.cmake"
        "include/tinyxml2.h"
        "lib/${_pans_tinyxml2_library}"
)

# -------- kcp --------
set(_pans_kcp_source "${PANS_THIRD_PARTY_DIR}/kcp")
pans_get_source_revision("${_pans_kcp_source}" _pans_kcp_revision)
file(SHA256 "${PROJECT_SOURCE_DIR}/cmake/kcp/CMakeLists.txt" _pans_kcp_wrapper_hash)
string(REPLACE "@NAME@" "kcp" _pans_kcp_library "${_pans_static_library}")
pans_add_cmake_dependency(
    NAME kcp
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/cmake/kcp"
    SOURCE_REVISION "${_pans_kcp_revision}"
    KEY_MATERIAL "wrapper=${_pans_kcp_wrapper_hash}"
    CMAKE_ARGS
        "-DPANS_KCP_SOURCE_DIR=${_pans_kcp_source}"
        "-DBUILD_SHARED_LIBS=OFF"
    SENTINELS
        "lib/cmake/kcp/kcp-config.cmake"
        "include/ikcp.h"
        "lib/${_pans_kcp_library}"
)

# -------- protobuf, 依赖预编译的 Abseil 与 zlib--------
set(_pans_protobuf_source "${PANS_THIRD_PARTY_DIR}/protobuf")
pans_get_source_revision("${_pans_protobuf_source}" _pans_protobuf_revision)
string(REPLACE "@NAME@" "protobuf" _pans_protobuf_library "${_pans_static_library}")
if(MSVC)
    string(REPLACE "@NAME@" "libprotobuf" _pans_protobuf_library "${_pans_static_library}")
endif()
set(_pans_protoc "bin/protoc${CMAKE_EXECUTABLE_SUFFIX}")
pans_add_cmake_dependency(
    NAME protobuf
    SOURCE_DIR "${_pans_protobuf_source}"
    SOURCE_REVISION "${_pans_protobuf_revision}"
    KEY_MATERIAL
        "abseil=${PANS_ABSEIL_CPP_CACHE_KEY}"
        "zlib=${PANS_ZLIB_CACHE_KEY}"
    DEPENDS
        "${PANS_ABSEIL_CPP_DEPENDENCY_TARGET}"
        "${PANS_ZLIB_DEPENDENCY_TARGET}"
    CMAKE_ARGS
        "-DCMAKE_CXX_STANDARD=20"
        "-DBUILD_SHARED_LIBS=OFF"
        "-Dprotobuf_BUILD_SHARED_LIBS=OFF"
        "-Dprotobuf_BUILD_TESTS=OFF"
        "-Dprotobuf_BUILD_CONFORMANCE=OFF"
        "-Dprotobuf_BUILD_EXAMPLES=OFF"
        "-Dprotobuf_LOCAL_DEPENDENCIES_ONLY=ON"
        "-Dprotobuf_WITH_ZLIB=ON"
        "-Dabsl_DIR=${PANS_ABSEIL_CPP_INSTALL_DIR}/lib/cmake/absl"
        "-DZLIB_ROOT=${PANS_ZLIB_INSTALL_DIR}"
        "-DZLIB_INCLUDE_DIR=${PANS_ZLIB_INSTALL_DIR}/include"
        "-DZLIB_LIBRARY=${PANS_ZLIB_INSTALL_DIR}/lib/${_pans_zlib_library}"
    SENTINELS
        "lib/cmake/protobuf/protobuf-config.cmake"
        "include/google/protobuf/message.h"
        "lib/${_pans_protobuf_library}"
        "${_pans_protoc}"
)

# RapidJSON 是纯头文件库，无需生成 output 缓存。
if(NOT TARGET rapidjson::rapidjson)
    add_library(rapidjson::rapidjson INTERFACE IMPORTED GLOBAL)
    set_target_properties(rapidjson::rapidjson PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${PANS_THIRD_PARTY_DIR}/rapidjson/include"
    )
endif()
message(STATUS "rapidjson: header-only; using ${PANS_THIRD_PARTY_DIR}/rapidjson/include")

add_custom_target(pans_third_party_dependencies ALL)
add_dependencies(pans_third_party_dependencies
    "${PANS_ABSEIL_CPP_DEPENDENCY_TARGET}"
    "${PANS_ZLIB_DEPENDENCY_TARGET}"
    "${PANS_YAML_CPP_DEPENDENCY_TARGET}"
    "${PANS_TINYXML2_DEPENDENCY_TARGET}"
    "${PANS_KCP_DEPENDENCY_TARGET}"
    "${PANS_PROTOBUF_DEPENDENCY_TARGET}"
)

# -------- pans 当前直接依赖 --------
find_package(Threads REQUIRED)

# sudo apt update
# sudo apt install libdw-dev
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PANS_LIBDW REQUIRED IMPORTED_TARGET libdw)
endif()

if(PANS_YAML_CPP_CACHE_COMPLETE)
    find_package(yaml-cpp CONFIG REQUIRED PATHS "${PANS_YAML_CPP_INSTALL_DIR}" NO_DEFAULT_PATH)
else()
    file(MAKE_DIRECTORY "${PANS_YAML_CPP_INSTALL_DIR}/include")
    add_library(yaml-cpp::yaml-cpp STATIC IMPORTED GLOBAL)
    set_target_properties(yaml-cpp::yaml-cpp PROPERTIES
        IMPORTED_LOCATION "${PANS_YAML_CPP_INSTALL_DIR}/lib/${_pans_yaml_library}"
        INTERFACE_COMPILE_DEFINITIONS YAML_CPP_STATIC_DEFINE
        INTERFACE_INCLUDE_DIRECTORIES "${PANS_YAML_CPP_INSTALL_DIR}/include"
    )
endif()

foreach(_pans_dependency_install_dir IN ITEMS
    "${PANS_YAML_CPP_INSTALL_DIR}"
    "${PANS_ABSEIL_CPP_INSTALL_DIR}"
    "${PANS_ZLIB_INSTALL_DIR}"
    "${PANS_PROTOBUF_INSTALL_DIR}"
    "${PANS_TINYXML2_INSTALL_DIR}"
    "${PANS_KCP_INSTALL_DIR}"
)
    install(
        DIRECTORY "${_pans_dependency_install_dir}/"
        DESTINATION "."
        PATTERN ".pans-cache-*" EXCLUDE
    )
endforeach()

install(
    DIRECTORY "${PANS_THIRD_PARTY_DIR}/rapidjson/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

unset(_pans_required_submodules)
unset(_pans_static_library)









# notes：cmake_parse_arguments() 主要处理三种关键字。
# 类型	        含义	            本例
# Option	不需要额外值的开关	没有
# One-value	后面跟一个值	NAME、SOURCE_DIR、SOURCE_REVISION
# Multi-value	后面可以跟多个值	CMAKE_ARGS、SENTINELS、DEPENDS、KEY_MATERIAL
#