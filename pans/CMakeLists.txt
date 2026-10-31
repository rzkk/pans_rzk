file(GLOB_RECURSE PANS_SOURCES CONFIGURE_DEPENDS 
#这种写法可以包含目录下所有的文件， 包括文件夹下的
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
                    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
# *.cc  
# *.c
)
file(GLOB_RECURSE PANS_HEADERS CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
)

add_library(pans STATIC ${PANS_SOURCES} ${PANS_HEADERS})  # 生成 libpans.a


# 告诉使用 pans 的其他 CMake 目标至少需要 C++20
target_compile_features(pans PUBLIC cxx_std_20)

# 允许静态库将来链接到共享库中 # 取代-fPIC
set_target_properties(pans PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(pans
    PUBLIC   # pans 公开的头文件
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE  # pans 不公开的头文件
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)


# pans_options 是项目内部的编译配置，不传递给外部使用者  // 使用private 
# target_link_libraries(pans PRIVATE pans_options) # 继承所有编译/链接选项
target_link_libraries(pans PRIVATE  pans_options)   # 使用 PRIVATE ， 外面的test 就无法使用pans cmake定义的细节 如PANS_DEBUG 


# 主库可能需要更严格的编译选项
target_compile_options(pans PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
        -Werror          # 将警告视为错误（主库严格要求）
    >
)
# if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
#     target_compile_options(pans PRIVATE -Werror)
# endif()



## 库的安装配置
#install()：定义安装规则，控制编译产物和头文件最终安装到哪里。
# 安装库文件
# 下面三个变量由 include(GNUInstallDirs) 提供
# ${CMAKE_INSTALL_LIBDIR} 是 CMake 中定义的库安装目录变量。  # 通常是 lib，某些系统为 lib64
#"${CMAKE_INSTALL_LIBDIR}  # 通常是 lib，某些系统为 lib64
#"${CMAKE_INSTALL_INCLUDEDIR}" # 通常是 include
install(TARGETS pans      #TARGETS 表示安装 CMake 中已经定义好的目标 ,这里的 pans 是 CMake 目标名称
    EXPORT pansTargets    #将 pans 目标加入名为 pansTargets 的导出集合，方便其他 CMake 项目导入和使用。
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"  # ARCHIVE 通常指静态库，以及 Windows DLL 对应的导入库。 DESTINATION 表示安装位置。
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"  # LIBRARY 主要对应 Unix/Linux 下的动态共享库，以及 CMake 的模块库。
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"  # RUNTIME 对应可执行程序，以及 Windows 下的 DLL 动态库。
)
# 安装公开头文件，保留 logger 等目录层级  
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/pans"  #保留层级， DIRECTORY 表示安装一个目录及其符合规则的内容。
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING            #只安装匹配后续 PATTERN 或 REGEX 规则的文件。
        PATTERN "*.h"
        PATTERN "*.hpp"
)

# install(EXPORT pansTargets

#     # 生成的文件名
#     FILE pansTargets.cmake

#     # 导入后使用 pans::pans
#     NAMESPACE pans::

#     # CMake 包相关文件安装位置
#     DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pans"
# )


