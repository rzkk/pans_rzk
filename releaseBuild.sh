#!/bin/bash
# 设置遇到错误立即退出，避免编译失败后仍然强行运行测试或程序
#它表示只要脚本中出现任何非零返回值的命令（例如 cmake --build 编译报错，或者 ctest 测试不通过），脚本就会立刻停止运行，不会去执行后面的旧程序。
# -e：命令失败时通常立即退出
# -u：使用未定义的变量时退出
# -o pipefail：管道中任何命令失败都视为失败
set -euo pipefail

# 定义变量，方便统一修改
BUILD_DIR="build/release"
# EXEC_PATH="./bin/tests"
# TEST_BUILD_DIR="$BUILD_DIR"


# rm -rf $BUILD_DIR   
# mkdir  $EXEC_PATH

# -S .   指定源码目录
# -B     指定构建目录
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

# --parallel：启用并行编译
# nproc：获取当前可用的 CPU 逻辑处理器数量
# CPU_NUM = $(nproc) *1.5
# cmake --build "$BUILD_DIR"  --parallel "($nproc)"
cmake --build "$BUILD_DIR"  --parallel 8


# --test-dir：指定测试所在的 构建目录  # 不是目标文件目录
# --output-on-failure：只在测试失败时输出详细信息
# --no-tests=error：未注册测试时将其视为错误
# ctest --test-dir "$TEST_BUILD_DIR" \
#       --output-on-failure \
#       --no-tests=error


# if  [ ! -f "$EXEC_PATH" ]; then
#     echo "❌ 错误: 未找到可执行文件 '$EXEC_PATH'。请检查 CMakeLists.txt 的输出路径设置。"
#     exit 1
# fi

# # -x：检查文件是否存在且具备执行权限
# if [ ! -x "$EXEC_PATH" ]; then
#     echo "❌ 错误：找不到可执行程序 $EXEC_PATH"
#     exit 1
# fi

# echo "-----------------------------------------------------------------------------------"
# "$EXEC_PATH"