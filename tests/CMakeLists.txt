file(GLOB PANS_TEST_SOURCES CONFIGURE_DEPENDS    # 作用就是 文件的增删 变化 会引发cmake 重新构建    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)


list(SORT PANS_TEST_SOURCES)

find_package(Threads REQUIRED)


# PARENT_SCOPE
set( PANS_TEST_OUTPUT_DIRECTORY   "${PROJECT_SOURCE_DIR}/bin/tests" )  #二进制输出目录
                                  #  "${CMAKE_BINARY_DIR}/bin/tests") # CMAKE_BINARY_DIR 指向整个 CMake 构建树的顶层目录

#每个 cpp 文件都是一个单独的测试文件
foreach(TEST_SOURCE IN  LISTS PANS_TEST_SOURCES)

    get_filename_component(TEST_NAME "${TEST_SOURCE}" NAME_WE)
    
    if(TARGET "${TEST_NAME}")
        message(FATAL_ERROR "Cannot create test target '${TEST_NAME}': a target with that name already exists")
    endif()
    
    add_executable("${TEST_NAME}"  "${TEST_SOURCE}")

    target_link_libraries("${TEST_NAME}" PRIVATE pans Threads::Threads dw)

    # 覆盖根目录默认的 bin 输出位置
    set_target_properties("${TEST_NAME}" PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PANS_TEST_OUTPUT_DIRECTORY}"
    )
    # 多配置生成器默认会追加 Debug/Release 子目录，这里保持输出路径一致。
    foreach(CONFIG IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${CONFIG}" CONFIG_UPPER)
        set_target_properties("${TEST_NAME}" PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER}" "${PANS_TEST_OUTPUT_DIRECTORY}"
        )
    endforeach()

    add_test(NAME "${TEST_NAME}" COMMAND "${TEST_NAME}")



endforeach()

# add_subdirectory(inner_tests)
