# 消息交互（长按菜单 / 引用 / 收藏 / 自定义表情）

> 与微信对齐的一套消息交互。前端实现集中在 `web/js/app.js`（交互）
> 与 `web/js/ui.js`（渲染），协议在 `proto/im_msg.proto`。

## 一、长按菜单

桌面端 **右键**、移动端 **长按 480ms** 都会在消息旁弹出菜单：

| 菜单项 | 出现条件 | 行为 |
|---|---|---|
| 复制 | 文本消息 | 写剪贴板 |
| 引用 | 全部消息 | 输入区上方出现引用条，下一条消息带上引用 |
| 收藏 | 全部消息 | 写入服务端收藏夹 |
| 撤回 | 自己发的消息、2 分钟内 | 消息变成「xxx 撤回了一条消息」 |
| 添加到表情 | 图片消息 | 存为自定义表情（本地） |
| 打开文件 | 文件/语音消息 | 新窗口打开原始文件 |
| 撤回（拍一拍） | 自己拍的一拍 | 同上，菜单里只有这一项 |

实现要点：`ui.js` 里 `attachMessageMenu()` 只负责**触发**（调
`UI.onMessageLongPress`），菜单 DOM 与动作由 `app.js` 的 `showMessageMenu()`
构建，避免 UI 层依赖 Store / 连接状态。

> ⚠️ 菜单出现后只在**点击别处**时关闭，不能监听 `contextmenu` 关闭——
> 右键事件会先冒泡到 document，把刚建好的菜单立刻关掉。

## 二、引用回复

- 协议：`ChatPrivate` / `ChatGroup` 增加 `quote_id` / `quote_text` / `quote_nickname`
- 落库：`messages` 表新增三列（`ALTER TABLE` 迁移，老库自动补）
- 展示：气泡顶部渲染引用块（昵称 + 原文摘要，超长省略）
- 限制：服务端把 `quote_id` 截到 64 字节、`quote_text` 截到 300 字节，只作展示用

## 三、收藏夹

- 协议：`FAVORITE_ADD_REQ(57)` / `LIST(59)` / `REMOVE(61)` 及对应 RSP
- 存储：`favorites(uid, msg_id, ...)`，**消息副本**，原消息删除不影响收藏
- 入口：左侧栏「收藏夹」（移动端底部 tab 第 4 项）
- 展示：文字 / 图片缩略图 / 语音时长 / 文件，带来源昵称与收藏时间
- 转发：卡片上「转发」→ 选会话 → 按原类型（文本/图片/文件/语音）发出

⚠️ **新增客户端消息类型时，必须同时改三处**，漏一处就会 400：
1. `common/include/im/common/msg_type.hpp`：枚举值 + `msgTypeName` + `isKnownMsgType` 上界
2. `im_server/src/core/json_codec.cpp` 的 `toBinary()`：加 `case`（WebSocket 入口靠它把
   JSON 转 protobuf，缺 case 会返回「消息字段不合法」400）
3. `im_server/src/core/chat_service.cpp` 的 `onPacket()` switch

## 四、自定义表情

- 长按图片消息 →「添加到表情」，URL 存入 `localStorage.pp_custom_emoji`（最多 24 个）
- 表情面板有「表情 / 收藏的表情」两个 tab，点自定义表情直接以图片消息发出
- 跨设备不同步（本地存储），如需同步可后续接入服务端

## 五、时间分隔规则

与微信一致：**跨天**显示日期；同一天内相邻消息**间隔 ≥ 2 分钟**才插一条时间，
连续聊天不再逐条显示时间行。实现见 `ui.js` 的 `renderMessages()`。

## 六、输入栏布局

```
[+]  [🎤]  [输入框 / 按住说话]  [☺]  [发送]
```

- `+`：展开下方功能面板（相册 / 文件 / 收藏 / 语音），展开时输入栏整体上移
- `🎤`：切换语音模式（按住说话，可勾选「说完转成文字」）
- `☺`：表情面板从底部滑出（不是浮在上方），含自定义表情 tab
- 输入框无 placeholder、无滚动条；高度只在换行时增长（单行固定 42px，不抖动）
- 输入横条（composer）上下各 4px 内边距，与底部 tab 栏零缝隙；发送按钮上下留白相等

## 七、撤回

- 协议：`RECALL_REQ(63)` / `RECALL_RSP(64)` / `RECALL_PUSH(65)`
- 服务端：只能撤回**自己发的**、**2 分钟内**、未撤回过的消息；`messages.recalled = 1`
  并把正文/附件清空，会话列表同步显示「[消息已撤回]」
- 推送：私聊推给对方，群聊广播给全群；前端把气泡替换成居中提示
- 语音、图片、文件、文本、拍一拍**全部可撤回**（同一条链路）

## 八、拍一拍

- 协议：`PAT_REQ(66)` / `PAT_RSP(67)` / `PAT_PUSH(68)`
- 触发：**双击对方头像**（320ms 内两次点击；移动端同样适用）
- 存储：落成一条 `msg_kind = 4` 的普通消息，因此可直接复用撤回链路
  - 私聊 `content` = 被拍者昵称，前端渲染「你拍了拍 X」/「X 拍了拍你」
  - 群聊 `content` = 被拍者昵称，前端渲染「A 拍了拍 B」
- 冷却：前端 800ms 内不重复发送，防连点刷屏
- 样式：`.system-msg.pat`，居中灰色小字，出现时 `patPop` 轻微弹一下

## 九、进入会话定位与历史加载

### 进入会话：必须在底部

消息按时间**从旧到新从上往下**排列，进入私聊 / 群聊时视口停在**最新一条**。

⚠️ 移动端 `.chat` 默认 `display: none`，点开会话时才加 `.mobile-open`。
此时 `renderMessages({forceBottom:true})` 里的 `scrollTop = scrollHeight` 写在
**不可见元素**上（`scrollHeight` 为 0）不生效。

因此统一走 `UI.scrollToBottom({ sticky: true })`：

1. 立即滚一次 + 连续两帧 `requestAnimationFrame` 各补一次（覆盖字体/布局）；
2. **粘性贴底**：`ResizeObserver` 监听 `.message-list`，图片等异步撑高后继续贴底，
   直到用户在 3 秒内主动滚动、或滚动位置离开底部 60px 以上（`UI.releaseStick()`）。

> 只滚一次是不够的：图片是 `loading="lazy"`，加载完成会把内容顶高，
> 视口就停在"半中间"——这正是"进去不是底部"的真实原因。

### 历史加载：从上往下扩展

- 首屏拉最近 **30 条**（`before_id = 0`），到底部；
- **滚动到顶部 80px 内自动加载**更早的 30 条（`loadOlder()`），不再需要点按钮；
  「查看更早的消息」按钮保留为兜底；
- 加载时记录"距底部距离"（`keepAnchor`），新内容插到上方后**视口内容不跳动**；
- 若加载后内容仍不满一屏，自动继续拉取，直到填满或 `hasMore = false`。


## 十、更换头像

**入口**：我 → 点头像行（或任意资料字段）→ 编辑资料 → 点头像行选图。

- 详情页的头像行是 `data-me="edit-avatar"` 按钮，点击后打开编辑页**并直接弹出选图**；
- 选图后前端用 canvas 压到 400px 再走 `UPLOAD_TICKET_REQ` + COS 直传，
  得到公网 URL 暂存在 `pendingAvatarUrl`；
- 点右上角「保存」才真正写库：`PROFILE_UPDATE_REQ`（`avatar_url`）→
  `users.avatar_url` → `broadcastOnline()` + `pushFriendList()`；
- 服务端校验：必须是 `https://`，且开启 COS 时域名必须属于本服务；
- 刷新页面后头像仍在（登录 / 静默恢复的响应都带 `avatarUrl`）。
