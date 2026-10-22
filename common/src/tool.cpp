// =============================================================================
//  common/src/tool.cpp
//  小的平台相关实现集中放这里，避免头文件里拖进 windows.h。
// =============================================================================
#include "im/common/tool.hpp"

#include <array>
#include <system_error>

namespace im {

std::string errText(int code) {
    if (code == 0) {
        return "ok";
    }
    // std::system_category 在 Windows 上会走 GetLastError 的文案，
    // 跨平台且不需要额外包含 windows.h。
    return std::system_category().message(code);
}

}  // namespace im
