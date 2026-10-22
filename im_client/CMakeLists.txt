# =============================================================================
#  im_client/CMakeLists.txt
#  Day1-Day5：控制台客户端（先把网络层调通）
#  Day6     ：再加 Qt6 GUI 目标，见 docs/ROADMAP.md
# =============================================================================

add_executable(im_client
    src/main.cpp
)

target_link_libraries(im_client PRIVATE im_common)
im_set_warnings(im_client)
set_target_properties(im_client PROPERTIES FOLDER "client")
