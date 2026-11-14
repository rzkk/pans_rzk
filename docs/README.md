# pans_rzk
多Reactor-多线程-多协程服务器框架 - 参考花间客老师和sylar老师
# pans

#### 介绍
多Reactor-多线程-多协程服务器框架

## 代码规范
常量：全部大写 
类名：首字母大写，驼峰 
非静态成员函数：首字母小写，驼峰 
静态成员函数：首字母大写，驼峰 
非静态成员变量：以m_开头，首字母小写，驼峰 
静态成员变量：以s_开头，首字母小写，驼峰 
全局函数：首字母大写，驼峰 
函数参数：小写，下划线隔开 
局部变量：小写，下划线隔开


## 文件夹说明
docs : 存放文档性内容
        * 维护 README 等说明文档
        * 重点：维护 中文版 Readme

tests : 测试程序
    * 验证代码的正确性与健壮性
    * 与核心代码分离存放
    inner_test : 专门测试 pans/src 内容 ， 测试不在外暴漏的功能 


tools : 非核心但重要的工具
    * 承担重要辅助功能
    * 例：Excel 导表工具

cmake : CMake 公共代码
    * 存放函数等公共 CMake 代码
    * 独立存放，供 CMakeLists.txt 包含
    *  CMake : 跨平台开源构建工具
    * 本身**不编译**，只生成各平台构建文件
    * 生成目标：`Makefile` / `sln` / `Ninja`


# 子模块管理

## 添加子模块    ### protobuf 的主分支是 main 
git submodule add -b master https://github.com/jbeder/yaml-cpp.git third_party/yaml-cpp

git submodule add -b master https://github.com/abseil/abseil-cpp.git third_party/abseil-cpp

git submodule add -b main https://github.com/protocolbuffers/protobuf.git third_party/protobuf

git submodule add -b master https://github.com/Tencent/rapidjson.git third_party/rapidjson

git submodule add -b master https://github.com/leethomason/tinyxml2.git third_party/tinyxml2

git submodule add -b master https://github.com/skywind3000/kcp.git third_party/kcp

git submodule add -b master https://github.com/madler/zlib.git third_party/zlib

## 同步代码
### 首次同步pans
git clone --recurse-submodules https://gitee.com/huajianke2026/pans.git

### 更新pans
如果已经同步过pans, 只需要更新其子模块，执行：
git submodule update --init --recursive

