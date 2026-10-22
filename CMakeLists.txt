# =============================================================================
#  pp —— 顶层 CMakeLists.txt
#  职责：定全局编译选项 / 找第三方依赖 / 生成 proto 代码 / 引入三个子项目
# =============================================================================
cmake_minimum_required(VERSION 3.21)

project(pp
    VERSION     0.1.0
    DESCRIPTION "C++17 + Asio + Protobuf 的网页版 IM"
    LANGUAGES   CXX)

# -----------------------------------------------------------------------------
# 全局编译选项（Day1 一次定好，后面所有目标自动受益）
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

if(MSVC)
    # /utf-8 : 源码按 UTF-8 解析，否则中文注释/字符串在 MSVC 下必然乱码
    # /MP    : 多进程并行编译，构建快一截
    add_compile_options(/utf-8 /MP)

    add_compile_definitions(
        _WIN32_WINNT=0x0A00   # 告诉 Asio 可以放心用 Win10 的新 Winsock API
        WIN32_LEAN_AND_MEAN   # 别让 windows.h 拖进一堆用不到的东西
        NOMINMAX              # 干掉 min/max 宏，否则和 std::min/std::max 打架
        ASIO_STANDALONE       # 强制独立版 Asio，不依赖 boost
    )
endif()

# 所有可执行文件集中输出到 build/bin（多配置生成器会自动再套一层 Debug/Release）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 在 VS 里按目录分组成"解决方案文件夹"，工程结构更清晰
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# 开高等级警告，但只加给我们自己的代码；protobuf 生成的代码不加（否则全是噪音）
function(im_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# -----------------------------------------------------------------------------
# 第三方依赖
#   - Windows / vcpkg：manifest 模式（默认）
#   - Linux / apt：设置 PP_USE_SYSTEM_DEPS=ON，不访问 vcpkg/GitHub
# -----------------------------------------------------------------------------
option(PP_USE_SYSTEM_DEPS "使用系统安装的依赖（apt 包），不经过 vcpkg" OFF)

if(PP_USE_SYSTEM_DEPS)
    find_package(Protobuf REQUIRED)
    find_package(spdlog   REQUIRED)
    find_package(SQLite3  REQUIRED)
    find_package(Threads  REQUIRED)

    # Ubuntu 的 libasio-dev 只有头文件，没有 CMake config，这里补一个 INTERFACE target
    find_path(PP_ASIO_INCLUDE_DIR NAMES asio.hpp REQUIRED)
    add_library(pp_asio INTERFACE)
    target_include_directories(pp_asio INTERFACE ${PP_ASIO_INCLUDE_DIR})
    target_compile_definitions(pp_asio INTERFACE ASIO_STANDALONE)
    target_link_libraries(pp_asio INTERFACE Threads::Threads)
    add_library(asio::asio ALIAS pp_asio)

    # 保持与 vcpkg 相同的 target 名字，业务代码不用改
    add_library(pp_sqlite INTERFACE)
    target_link_libraries(pp_sqlite INTERFACE SQLite::SQLite3)
    add_library(unofficial::sqlite3::sqlite3 ALIAS pp_sqlite)
else()
    find_package(Protobuf CONFIG REQUIRED)
    find_package(spdlog   CONFIG REQUIRED)
    find_package(asio     CONFIG REQUIRED)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
endif()

# -----------------------------------------------------------------------------
# im_proto：把 proto/*.proto 编译成 im_msg.pb.h / im_msg.pb.cc，打包成静态库
#           —— 服务端和客户端链接同一个 im_proto，协议天然不会走偏
# -----------------------------------------------------------------------------
file(GLOB IM_PROTO_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto)

add_library(im_proto STATIC)
target_sources(im_proto PRIVATE ${IM_PROTO_FILES})

# protobuf 官方 CMake 包提供的函数：调用 protoc 生成代码并挂到 target 上
protobuf_generate(TARGET im_proto LANGUAGE cpp)

# 生成的 im_msg.pb.h 落在 build/proto/（保留 proto/ 相对路径），
# 所以把 build 根和 build/proto 一起设为公共 include
target_include_directories(im_proto PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/proto
)
target_link_libraries(im_proto PUBLIC protobuf::libprotobuf)

# -----------------------------------------------------------------------------
# im_common：服务端 + 客户端共用的基础层（协议工具 / Buffer / 消息封装）
#            Day2 会往 common/src/ 里加 buffer.cpp、proto_msg.cpp，
#            在这里的源文件列表追加一行即可，两边同时生效。
# -----------------------------------------------------------------------------
add_library(im_common STATIC
    common/src/tool.cpp
    common/src/crypto.cpp
)

target_include_directories(im_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common/include)
target_link_libraries(im_common PUBLIC im_proto spdlog::spdlog asio::asio)
im_set_warnings(im_common)
set_target_properties(im_common PROPERTIES FOLDER "common")

# -----------------------------------------------------------------------------
# 子项目
# -----------------------------------------------------------------------------
add_subdirectory(im_server)
add_subdirectory(im_client)
add_subdirectory(tests)
