# 表示判断一个变量是否已经定义。  是否定义与 是否是假 完全不同
# if(DEFINED 变量名)
#     ...
# endif()
# 应该改成下面的样子 ： 即判断是否定义 还判断是否为空
    # if(NOT DEFINED PANS_DEPS_STAMP_FILE
    #    OR "${PANS_DEPS_STAMP_FILE}" STREQUAL ""
    #    OR NOT DEFINED PANS_DEPS_STAMP_VALUE
    #    OR "${PANS_DEPS_STAMP_VALUE}" STREQUAL "")

## 1. 检查必需的变量是否已经定义
if(NOT DEFINED PANS_DEPS_STAMP_FILE OR NOT DEFINED PANS_DEPS_STAMP_VALUE)
    message(FATAL_ERROR "Dependency stamp file and value are required")
endif()

# Create the directory for the stamp file if it doesn't exist

## get_filename_component( <输出变量> <文件路径>  <提取模式> )
    # 提取模式 ： 模式	                作用
                # DIRECTORY	        提取文件所在目录
                # NAME	            提取文件名
                # EXT	            提取扩展名
                # NAME_WE	        提取不包含扩展名的文件名
                # ABSOLUTE	        获取绝对路径
## file(MAKE_DIRECTORY <目录路径>...)   #作用是创建指定目录。如果父目录尚不存在，也会根据需要创建。
## file(WRITE <文件路径> <内容>...)  #如果文件不存在，就创建文件。  如果文件已经存在，WRITE 会覆盖其原有内容 
##file(APPEND <文件路径> <内容> ... ) # APPEND 表示追加内容，而不是覆盖。
# 2. 获取缓存标记文件所在的目录
get_filename_component(stamp_directory "${PANS_DEPS_STAMP_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${stamp_directory}")# 3. 如果目录不存在，就创建目录
file(WRITE "${PANS_DEPS_STAMP_FILE}" "${PANS_DEPS_STAMP_VALUE}\n") # 4. 将缓存 Key 写入标记文件



