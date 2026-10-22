# pp 统一身份登录（OAuth 2.0 授权码模式）

> 一句话：pp 是身份提供方（IdP），校捷通是接入方（RP）。用户在校捷通的登录页点
> 「使用 pp 账号登录」，跳到 pp 授权页确认后，校捷通拿到一次性 code，换回自己的 JWT。

---

## 1. 时序

```
浏览器                校捷通后端                         pp 服务端
  │  GET /auth/pp/start   │                                 │
  ├──────────────────────>│  签发 state（5min JWT，含 next）  │
  │  302 pp/oauth/authorize?client_id&redirect_uri&state    │
  │<──────────────────────┤                                 │
  │ ──────────────────────────────────────────────────────>│  授权页
  │  用户在 pp 登录并点「同意并继续」                          │
  │  POST pp/oauth/authorize  (Bearer pp 会话令牌)          │
  │ ──────────────────────────────────────────────────────>│  签发 code(60s)
  │  302 校捷通 /auth/pp/callback?code&state                │
  │<──────────────────────────────────────────────────────┤
  │ ─────────────────────>│  校验 state                       │
  │                       │  POST pp/oauth/token ───────────>│  校验 client_secret
  │                       │  <──────────── access_token(1h)  │
  │                       │  按 pp_uid 查/建本地用户           │
  │                       │  签发 pp_ticket(5min, jti 一次性)  │
  │  302 /login.html?pp_ticket=...&next=/                   │
  │<──────────────────────┤                                 │
  │  POST /auth/pp/exchange {ticket}                        │
  │ ─────────────────────>│  消费 jti → 返回本校 JWT           │
  │<──────────────────────┤                                 │
```

要点：

- **code 只走服务端对服务端**，client_secret 永远不出现在浏览器。
- **ticket 一次性**：`jti` 在进程内消费表里登记，重复 exchange 直接 401。
- **redirect_uri 精确白名单**：pp 侧 `oauth_clients.redirect_uris` 逗号分隔，不做前缀匹配。
- **state 是签名的 JWT**（5 分钟），回调时校验，防 CSRF。
- 校捷通不新建一套令牌体系，直接复用现有 JWT（`uid/role/tv/type`），
  所以 `/api/v1` 下所有业务接口零改动。

---

## 2. pp 侧配置

`/opt/pp/config/oauth.env`（`chmod 600`，属主与 `pp.service` 的 `User=` 一致）：

```bash
PP_OAUTH_CLIENT_ID=xiaojietong-web
PP_OAUTH_CLIENT_SECRET=<32 字节随机 hex>
PP_OAUTH_CLIENT_NAME=校捷通
PP_OAUTH_REDIRECT_URIS=https://xiaojietong.online/api/v1/auth/pp/callback
```

`pp.service` 已包含：

```ini
EnvironmentFile=-/opt/pp/config/oauth.env
```

启动日志会打印 `[oauth] 接入方已登记: xiaojietong-web (1 个回调地址)`。
每次启动都按环境变量**幂等刷新**接入方，改回调白名单不需要动数据库。

### 数据表

```sql
oauth_clients(client_id, client_secret_hash, name, redirect_uris, created_at)
auth_tokens(token_hash, kind, uid, client_id, redirect_uri, expires_at, used, created_at)
```

`kind`：`1` pp 会话令牌（7 天）/ `2` 授权码（60 秒）/ `3` access_token（1 小时）。
库里只存 `SHA-256(明文)`，即使数据库泄露也拿不到可用令牌。

### HTTP 端点

| 方法 | 路径 | 用途 |
|---|---|---|
| GET | `/oauth/authorize` | 授权确认页（静态 `web/oauth.html`） |
| POST | `/oauth/authorize` | 用户同意，Bearer=pp 会话令牌，返回拼好 code 的回跳地址 |
| POST | `/oauth/token` | code 换 access_token（`application/x-www-form-urlencoded` 或 JSON） |
| GET | `/oauth/userinfo` | Bearer=access_token，返回 uid / pp_id / nickname / avatar_url |
| POST | `/api/login` | 授权页用：账号（用户名或 pp 号）+ 密码换 pp 会话令牌 |
| GET | `/api/session` | 校验 pp 会话令牌 |
| POST | `/api/logout` | 撤销 pp 会话令牌 |
| GET | `/api/grants` | 授权管理：我授权过的应用（Bearer=pp 会话令牌） |
| POST | `/api/grants/revoke` | 解除授权：`{"clientId":"..."}`，删除该应用全部 access_token |
| POST | `/oauth/revoke` | **接入方服务端**撤销某用户授权：`client_id + client_secret + uid`（校捷通解绑 pp 时调用） |

### 授权管理页

`https://pp.xiaojietong.online/grants.html`（入口：pp「我 → 授权管理」）

- 按 `client_id` 聚合 `kind=3` 的 access_token，显示应用名、最近授权时间、是否仍有效
- 「解除授权」删除该用户在该应用下的全部 access_token —— **已发出去的令牌立即失效**
- 列表来自 `auth_tokens`，不需要额外的授权关系表（授权即发令牌）


### WebSocket 新增

| 编号 | 名称 | 说明 |
|---|---|---|
| 55 | `SESSION_LOGIN_REQ` | 用 `LoginRsp.token` 静默恢复登录 |
| 56 | `SESSION_LOGIN_RSP` | 复用 `LoginRsp` 结构 |

`LOGIN_RSP` 新增字段 `token`（7 天会话令牌），前端存 `localStorage.pp_token`，
刷新页面不再要求重新输密码。

---

## 3. 校捷通侧配置

`/opt/xiaojietong/shared/server.env`：

```bash
XJT_PP_OAUTH_BASE=https://pp.xiaojietong.online
XJT_PP_CLIENT_ID=xiaojietong-web
XJT_PP_CLIENT_SECRET=<与 pp 侧一致>
XJT_PUBLIC_BASE_URL=https://xiaojietong.online
# 留空 = XJT_PUBLIC_BASE_URL + /api/v1/auth/pp/callback
XJT_PP_REDIRECT_URI=
# 浏览器直接访问的路径必须豁免内部访问闸门
XJT_ACCESS_GATE_EXEMPT=/api/v1/health,/api/v1/auth/pp/*
```

### 数据库（A 档：独立 pp_uid 列）

`db/sql/21_user_pp.sql`：

- `user.openid` 改为**允许 NULL**（微信用户仍有值；pp / 本地账号用户为 NULL）
- 新增 `pp_uid BIGINT UNSIGNED NULL` + `UNIQUE uk_pp_uid`
- 新增 `pp_id VARCHAR(32)`（展示用，如 `pp100001`）
- 新增 `idx_phone`，供「手机号 + 密码」登录

### C++ 数据层

`UserDAO` 新增：

```
find_by_pp_uid / find_by_phone / find_by_student_no
create_pp_user / create_local_user
bind_pp / set_password_hash / touch_login
```

⚠️ 改完必须重编 `jt_db`：

```bash
cd /opt/xiaojietong/app/db/cpp_driver
sudo -u xjt env HOME=/tmp cmake --build build -j2
# 产物直接输出到 backend/app/db/native/jt_db.so
sudo systemctl restart xiaojietong-api
```

### 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/v1/auth/pp/start?next=/` | 302 到 pp 授权页 |
| GET | `/api/v1/auth/pp/callback` | pp 回跳，换 code → 302 回登录页带 ticket |
| POST | `/api/v1/auth/pp/exchange` | `{ticket}` → `{token, refresh_token, user}` |
| POST | `/api/v1/auth/pp/bind-start` | 已登录用户发起绑定：返回 `{url}`（pp 授权地址） |
| POST | `/api/v1/auth/pp/unbind` | 解绑 pp（要求账号还有微信或密码，否则拒绝 3004；成功后联动撤销 pp 授权） |
| POST | `/api/v1/user/phone` | 绑定 / 换绑手机号（唯一校验；当前无短信验证码） |
| POST | `/api/v1/auth/register` | `{phone, password, nickname}` 本地注册并登录 |
| POST | `/api/v1/auth/login` | `{account, password}`，手机号或学号 |
| POST | `/api/v1/auth/password` | 已登录后设置 / 修改密码 |
| POST | `/api/v1/auth/logout` | 登出：`token_version + 1`，所有已签发令牌立即失效 |

### 已有账号绑定 pp

`state` 里带 `mode=bind` 与当前校捷通 `uid`，callback 走 `_complete_bind()`：

1. 校验当前账号存在、尚未绑定
2. 校验该 pp 账号未被其他校捷通账号占用（`uk_pp_uid`）
3. `bind_pp(uid, pp_uid, pp_id)`（SQL 带 `AND pp_uid IS NULL`，天然防并发重复绑定）
4. 302 到 `/?pp_bind=ok#/me`，前端 toast 提示并清掉 query

失败时 302 到 `/?pp_bind=error&pp_bind_msg=...`，不会创建任何账号。

### 解绑 pp（安全约束）

`POST /api/v1/auth/pp/unbind`（Bearer 校捷通 JWT）：

1. 未绑定 → `3003`
2. **账号既无 `openid`（微信）又无 `password_hash` → 拒绝 `3004`**
   —— 纯 pp 账号解绑后会彻底无法登录（没有可用登录方式）
3. `UPDATE user SET pp_uid=NULL, pp_id=''`
4. **联动撤销 pp 侧授权**：服务端用 `client_secret` 调 pp `/oauth/revoke`
   （best-effort，失败只记日志，不影响解绑结果）

前端「我的」页：已绑定时右侧显示红色「解绑」，点击弹二次确认（明确告知"解绑后将不能用 pp 登录"），
成功后行变回「绑定 pp 账号」。

### 授权范围（scope）

- 校捷通发起授权时带 `scope=profile`，pp 只授予 `profile`（头像 / 昵称 / pp 号）
- pp **不接受**客户端自定义 scope：未知值一律按 `profile` 处理，避免授权页显示虚假权限
- `auth_tokens.scope` 持久化该值，授权管理页展示「权限：头像、昵称、pp 号」


密码存储：`PBKDF2-HMAC-SHA256`，20 万次迭代，格式
`pbkdf2_sha256$<iterations>$<salt_hex>$<hash_hex>`，校验用 `hmac.compare_digest`。

### 前端页面

| 文件 | 作用 |
|---|---|
| `web/login.html` | 登录方式选择页：pp 登录 / 校捷通账号登录 |
| `web/login-password.html` | 校捷通账号登录 + 注册 |
| `web/js/api.js` | 无 token → `location.replace('/login.html?next=...')`；401 同样跳转 |
| `miniprogram/pages/user/password` | 小程序「设置登录密码」：已绑手机号只需密码，否则同时补绑手机号 |
| `miniprogram/pages/user/phone` | 小程序「手机号」：绑定 / 换绑手机号（`POST /user/phone`） |

`next` 参数只接受以 `/` 开头的站内路径（后端与前端都做校验），防开放重定向。

---

## 4. 联调脚本

| 脚本 | 说明 |
|---|---|
| `lab/10_oauth/test_oauth.py` | pp 本地：授权码全流程 + 重放 / 白名单 / 错误 secret |
| `lab/10_oauth/test_sso_prod.py` | 生产：注册临时 pp 账号 → 走完 SSO → 用校捷通 JWT 调 `/user/me` |
| `lab/10_oauth/cleanup_sso.py` | 清理联调产生的临时账号 |

```bash
python lab/10_oauth/test_oauth.py http://127.0.0.1:8080 oauthuser test1234
python lab/10_oauth/test_sso_prod.py
```

---

## 5. 排障

| 现象 | 排查 |
|---|---|
| **页面一直闪动 / 无法登录** | 多半是 `/login.html` 没部署成功（被 SPA fallback 成 index.html），于是「index → login → index」无限循环。`api.js` 已内置 3 秒跳转保护并在页面上直接提示；部署时确认 `curl -s https://域名/login.html` 返回的是登录页而不是首页 |
| 跳到 pp 后提示「缺少 client_id 或 redirect_uri」 | 校捷通 `/auth/pp/start` 的 302 地址被代理截断 |
| pp 提示「回调地址不在白名单内」 | `PP_OAUTH_REDIRECT_URIS` 与 `XJT_PP_REDIRECT_URI` 必须逐字符一致（含协议、端口、结尾斜杠） |
| 回调后回到登录页并提示「登录凭据已使用」 | 同一个 ticket 被用了两次；或前端重复提交 exchange |
| 回调 401「登录凭据已过期」 | ticket 5 分钟过期，重新点 pp 登录 |
| 校捷通日志 `pp 授权失败` | 看 pp 的 `journalctl -u pp`，通常是 client_secret 不一致 |
| H5 一直跳登录页 | 浏览器 localStorage 没有 `xjt_web_token`，或 `/api/v1/user/me` 返回 401（token 过期） |
| 小程序登录不受影响 | 小程序继续走 `wx.login` → `/auth/wechat-login`，与 pp 登录并行 |
