# myIM —— 项目总纲（唯一权威需求文档）

> 本文件是项目的"宪法"。**每次开新会话时，让 AI 读这个文件即可恢复全部上下文**，
> 不要把 7 天计划全文粘贴进聊天窗口（会拖爆上下文导致模型无响应）。

---

## 0. 目标与约束

- 目标：**一周 7 天 × 2h = 14h**，做出可运行的最小可用 IM（注册 / 登录 / 文本私聊 / 心跳）
- 定位：大厂 C++ 后端求职项目，重点吃透 **Reactor、TCP 分包、长连接 Session 管理**
- 技术栈：C++17 + 独立版 Asio + Protobuf + SQLite3 + spdlog + Qt6 + CMake + vcpkg
- 核心原则：**先控制台跑通网络层，网络稳定后再接 Qt GUI**（严禁 GUI 与网络 bug 混在一起调）

## 1. 范围（严格冻结）

**做**：控制台服务端（Asio Reactor 异步 TCP、多客户端长连接）/ 4 字节大端长度包头 + Protobuf 包体 /
SQLite 用户表与注册登录校验 / `shared_ptr` 管理 Session 生命周期 + 在线用户映射 / 心跳超时踢死连接 /
私聊转发 / Qt6 登录窗口 + 主窗口（在线用户列表 + 聊天区）/ spdlog 日志。

**不做（一周内绝对不碰）**：好友申请、群聊、文件传输、离线消息、图片头像、AES 加密、断线重连、
消息本地持久化、UI 美化、消息已读状态。

## 2. 目录结构（相对 `e:\myIM`）

```
myIM/
├── CMakeLists.txt              顶层：全局编译选项 / 找依赖 / 生成 proto / 引入子项目
├── CMakePresets.json           预设：default 配置 + relwithdebinfo/release/debug 构建
├── vcpkg.json                  依赖清单（manifest 模式）
├── docs/ROADMAP.md             本文件
├── proto/
│   └── im_msg.proto            全部协议定义（服务端客户端共用一份）
├── common/                     ★ 服务端客户端共用层，编译成 im_common 静态库
│   ├── include/im/common/
│   │   ├── tool.hpp            大端长度头编解码 / hex 打印 / errText
│   │   ├── msg_type.hpp        MsgType 枚举 + 名字映射 + 合法性校验
│   │   ├── buffer.hpp          [Day2] 分包缓冲区
│   │   └── proto_msg.hpp       [Day2] protobuf 消息收发封装
│   └── src/
│       ├── tool.cpp
│       ├── buffer.cpp          [Day2]
│       └── proto_msg.cpp       [Day2]
├── im_server/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp            入口：日志 + io_context + Server
│       ├── server.hpp/cpp      [Day2] acceptor
│       ├── session.hpp/cpp     [Day2] 单连接：异步读写 + 分包 + 分发
│       ├── user_mgr.hpp/cpp    [Day3] 在线用户表
│       ├── db.hpp/cpp          [Day3] SQLite 封装
│       └── msg_handler.hpp/cpp [Day3] 业务消息分发
├── im_client/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp            Day1-5 控制台，Day6 起为 Qt main
│       ├── net_client.hpp/cpp  [Day5]
│       └── ui/
│           ├── loginwindow.hpp/cpp/ui  [Day6]
│           └── mainwindow.hpp/cpp/ui   [Day6]
└── tests/
    ├── CMakeLists.txt
    ├── day1_proto_smoke.cpp    Day1 自检
    └── ...                     每天一个自检程序
```

### 与原计划的 3 处偏离（均已论证，非擅自简化）

1. `im_server/proto/` → **顶层 `proto/`**。协议是两端共用的，放服务端目录下客户端要靠软链接/复制取用；
   Windows 建软链接需要开发者模式或管理员权限，纯属自找麻烦。
2. `im_server/src/common/` + `im_client/common/` → **合并为顶层 `common/` + 单一 CMake 目标 `im_common`**。
   两端 `target_link_libraries(... im_common)` 即可，代码零重复、改一处两端同时生效。
3. 构建配置默认用 **RelWithDebInfo** 而非 Debug。vcpkg `x64-windows` 三元组产出的是 /MD（Release CRT）
   编译的 protobuf DLL；若我们的程序用 /MDd（Debug CRT），protobuf 返回的 `std::string` 跨 CRT 析构会
   偶发崩溃且报错位置极具误导性。RelWithDebInfo 有完整 PDB、可断点，同时 CRT 与 vcpkg 一致。

### 协议补全（Day1 发现的原计划缺口）

| 缺口 | 后果 | 补全 |
|---|---|---|
| 原 proto 无注册消息 | Day3/Day6/Day7 全要求"注册账号 A/B"，注册按钮无协议可用 | 新增 `REGISTER_REQ=5` / `REGISTER_RSP=6` |
| 无任何消息能下发在线列表 | Day6 主窗口"在线用户列表"无数据来源 | 新增 `ONLINE_USERS_PUSH=7`（登录成功后 + 有人上下线时推全量列表） |

`ChatPrivate.from_uid` 由客户端填写，仅视为"声明"；**服务端必须用会话记录的 uid 覆盖**，
否则任何客户端都能伪造他人身份发消息（Day4 处理）。

## 3. 7 天任务与验收标准

| Day | 主题 | 必须达成的验收标准 |
|---|---|---|
| 1 | 环境 + 协议 | vcpkg 依赖装好；protoc 生成 `im_msg.pb.h/.cc`；`day1_proto_smoke` 输出 `PASSED` |
| 2 | Buffer + Session + Server | 服务端能接受多连接；粘包/半包被正确切开；临时控制台客户端收发原始帧 |
| 3 | DB + UserManager + 登录 | 两个控制台客户端注册并登录，服务端返回成功/失败并维护在线表 |
| 4 | 私聊转发 + 心跳 | 两个客户端互发文本；拔网线 60s 内服务端踢掉死连接并清理在线表 |
| 5 | 客户端网络层 | `net_client` 在控制台独立跑通登录 + 收发 + 定时心跳 |
| 6 | Qt UI 绑定 | GUI 能注册/登录、看到在线列表、发消息并实时收到 |
| 7 | 联调 + 收尾 | 端到端完整流程通过；异常退出无残留 session；简历描述定稿 |

## 4. 面试必答（写代码时就要刻意营造证据）

1. **Buffer 分包**：TCP 无边界 → 4 字节大端长度头；缓冲内部维护读偏移，避免频繁内存分配与
   每次 `erase` 前移。防御：长度字段做 1 MiB 上限，非法长度立刻断开。
2. **Session 生命周期**：`std::enable_shared_from_this<Session>`；每个异步回调捕获
   `self = shared_from_this()`，保证回调执行期间对象不被析构；`close()` 后引用计数归零自动回收。
3. **Reactor**：`io_context` 即 Reactor 事件循环；`async_accept/async_read/async_write` 注册事件，
   `io_context.run()` 驱动派发。
4. **心跳与半开连接**：TCP FIN/RST 感知不到拔网线、断电等"半开"状态；靠应用层心跳超时剔除死连接。
5. **多线程模型**：网络 IO 线程与 Qt UI 线程解耦；共享资源（在线用户表）加锁；跨线程只用信号槽，
   子线程**严禁**直接碰 Qt 控件。

## 5. 风险控制

1. **分层验证**：每天结束时工程必须能编译，并且当天的自检程序能跑。
2. **卡壳 30 分钟规则**：标记 TODO 写占位代码跳过，保证整体流程推进，Day7 统一清账。
3. **绝不临时加需求**：任何"顺便加个头像/文件传输"的念头直接掐死。
4. **上下文纪律**：每完成一个阶段开新会话，新会话第一句 = "读 `docs/ROADMAP.md` 后继续 Day N"。

## 6. 构建速查

```powershell
# 一次性：装 vcpkg、设 VCPKG_ROOT（见 Day1 记录）
# 配置 + 构建（文件生成到 build/bin/RelWithDebInfo/）
cmake --preset default
cmake --build --preset relwithdebinfo

# 跑自检
.\build\bin\RelWithDebInfo\day1_proto_smoke.exe
```
