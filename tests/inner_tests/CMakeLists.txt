file(GLOB PANS_INNER_TEST_SOURCES CONFIGURE_DEPENDS    # 作用就是 文件的增删 变化 会引发cmake 重新构建    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

list(SORT PANS_INNER_TEST_SOURCES)

find_package(Threads REQUIRED)


# PARENT_SCOPE
set( PANS_INNER_TEST_OUTPUT_DIRECTORY   "${PROJECT_SOURCE_DIR}/bin/tests_inner" )  #二进制输出目录
                                  #  "${CMAKE_BINARY_DIR}/bin/tests") # CMAKE_BINARY_DIR 指向整个 CMake 构建树的顶层目录

#每个 cpp 文件都是一个单独的测试文件
foreach(INNER_TEST_SOURCE IN LISTS PANS_INNER_TEST_SOURCES)

    get_filename_component(INNER_TEST_NAME "${INNER_TEST_SOURCE}" NAME_WE) # 从带路径的文件名取出 文件名
    
    # 因为是已经排过序的 ， 可以判断文件是否已经存在
    if(TARGET "${INNER_TEST_NAME}")
        message(FATAL_ERROR "Cannot create test target '${INNER_TEST_NAME}': a target with that name already exists")
    endif()
    
    add_executable("${INNER_TEST_NAME}"  "${INNER_TEST_SOURCE}")

    target_link_libraries("${INNER_TEST_NAME}" PRIVATE  pans  Threads::Threads dw) #dw

    target_include_directories("${INNER_TEST_NAME}" PRIVATE 
                "${PROJECT_SOURCE_DIR}/pans/src")

    # 覆盖根目录默认的 bin 输出位置
    set_target_properties("${INNER_TEST_NAME}" PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PANS_INNER_TEST_OUTPUT_DIRECTORY}"
    )
    
    # 多配置生成器默认会追加 Debug/Release 子目录，这里保持输出路径一致。
    foreach(CONFIG IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER "${CONFIG}" CONFIG_UPPER)
        set_target_properties("${INNER_TEST_NAME}" PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER}" "${PANS_INNER_TEST_OUTPUT_DIRECTORY}"
        )
    endforeach()

    add_test(NAME "${INNER_TEST_NAME}" COMMAND "${INNER_TEST_NAME}")



endforeach()














