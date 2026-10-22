# =============================================================================
#  im_server/CMakeLists.txt
#  服务端：HTTP/WebSocket + 原始 TCP 双链路 + SQLite 持久化。
# =============================================================================

add_executable(im_server
    src/main.cpp
    src/core/database.cpp
    src/core/chat_service.cpp
    src/core/json_codec.cpp
    src/core/cos.cpp
    src/net/tcp_session.cpp
    src/net/tcp_server.cpp
    src/web/http_server.cpp
    src/web/http_session.cpp
    src/web/static_files.cpp
    src/web/websocket.cpp
)

target_include_directories(im_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(im_server PRIVATE im_common unofficial::sqlite3::sqlite3)
im_set_warnings(im_server)
set_target_properties(im_server PROPERTIES FOLDER "server")

# 把 web/ 目录复制到可执行文件旁边，双击 exe 就能直接访问。
# 用 ALL 自定义目标而不是 POST_BUILD：改 CSS/JS 后无需重链接 exe 也能同步。
add_custom_target(im_web_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/web
            $<TARGET_FILE_DIR:im_server>/web
    COMMENT "同步网页资源")
add_dependencies(im_server im_web_assets)

