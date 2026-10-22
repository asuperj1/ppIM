# =============================================================================
#  tests/CMakeLists.txt
#  每天一个自检小程序，保证"当天代码能独立跑通"再往下走。
# =============================================================================

add_executable(day1_proto_smoke day1_proto_smoke.cpp)
target_link_libraries(day1_proto_smoke PRIVATE im_common)
im_set_warnings(day1_proto_smoke)
set_target_properties(day1_proto_smoke PROPERTIES FOLDER "tests")
