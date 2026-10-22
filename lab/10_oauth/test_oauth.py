"""pp OAuth 端到端联调（不依赖浏览器）。

覆盖：
  1. POST /api/login            账号密码换 pp 会话令牌
  2. GET  /api/session          用会话令牌确认身份
  3. POST /oauth/authorize       用户同意授权 -> 一次性 code
  4. POST /oauth/token           code 换 access_token（服务端对服务端）
  5. GET  /oauth/userinfo        access_token 取用户资料
  6. 重放同一个 code             必须失败（一次性）
  7. 错误 client_secret          必须失败
  8. 未授权 redirect_uri         必须失败

用法：
  python test_oauth.py [base_url] [username] [password]
"""
from __future__ import annotations

import json
import sys
import urllib.error
import urllib.parse
import urllib.request

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8080"
USERNAME = sys.argv[2] if len(sys.argv) > 2 else "oauthuser"
PASSWORD = sys.argv[3] if len(sys.argv) > 3 else "test1234"

CLIENT_ID = "xiaojietong-web"
CLIENT_SECRET = "test-secret"
REDIRECT_URI = "http://127.0.0.1:8080/api/v1/auth/pp/callback"

passed = 0
failed = 0


def request(path: str, method: str = "GET", body: dict | None = None,
            token: str = "", form: dict | None = None) -> tuple[int, dict]:
    data = None
    headers = {"Accept": "application/json"}
    if form is not None:
        data = urllib.parse.urlencode(form).encode()
        headers["Content-Type"] = "application/x-www-form-urlencoded"
    elif body is not None:
        data = json.dumps(body).encode()
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token

    req = urllib.request.Request(BASE + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=10) as rsp:
            raw = rsp.read().decode()
            return rsp.status, (json.loads(raw) if raw else {})
    except urllib.error.HTTPError as err:
        raw = err.read().decode()
        return err.code, (json.loads(raw) if raw else {})


def check(name: str, ok: bool, detail: str = "") -> None:
    global passed, failed
    if ok:
        passed += 1
        print(f"  [PASS] {name}")
    else:
        failed += 1
        print(f"  [FAIL] {name} {detail}")


print(f"== pp OAuth 联调 @ {BASE} ==")

# 1. 登录
status, rsp = request("/api/login", "POST", {"username": USERNAME, "password": PASSWORD})
check("POST /api/login 返回 200", status == 200, f"status={status} rsp={rsp}")
token = rsp.get("token", "")
check("拿到 pp 会话令牌", bool(token))
check("返回 pp 号", bool(rsp.get("ppId")), str(rsp))

# 2. 会话校验
status, rsp = request("/api/session", token=token)
check("GET /api/session 校验通过", status == 200 and rsp.get("success"))
uid = rsp.get("uid")

# 3. 授权
status, rsp = request("/oauth/authorize", "POST", {
    "client_id": CLIENT_ID,
    "redirect_uri": REDIRECT_URI,
    "state": "abc123",
}, token=token)
check("POST /oauth/authorize 成功", status == 200 and rsp.get("success"), str(rsp))
redirect = rsp.get("redirect", "")
query = urllib.parse.parse_qs(urllib.parse.urlparse(redirect).query)
code = (query.get("code") or [""])[0]
check("回跳地址带 code 与 state", bool(code) and query.get("state") == ["abc123"], redirect)

# 3b. 未授权回调地址必须被拒
status, rsp = request("/oauth/authorize", "POST", {
    "client_id": CLIENT_ID,
    "redirect_uri": "https://evil.example.com/cb",
    "state": "x",
}, token=token)
check("非白名单 redirect_uri 被拒", status == 400 and not rsp.get("success"), str(rsp))

# 4. 换令牌
status, rsp = request("/oauth/token", "POST", form={
    "grant_type": "authorization_code",
    "code": code,
    "client_id": CLIENT_ID,
    "client_secret": CLIENT_SECRET,
    "redirect_uri": REDIRECT_URI,
})
check("POST /oauth/token 换到 access_token", status == 200 and bool(rsp.get("accessToken")), str(rsp))
access = rsp.get("accessToken", "")
check("access_token 绑定正确用户", str(rsp.get("uid")) == str(uid), f"{rsp.get('uid')} vs {uid}")
# proto3 JSON 把 int64 输出成字符串，这里按数值比较
check("expires_in = 3600", int(rsp.get("expiresIn", 0)) == 3600, str(rsp.get("expiresIn")))

# 4b. code 重放必须失败
status, rsp = request("/oauth/token", "POST", form={
    "grant_type": "authorization_code",
    "code": code,
    "client_id": CLIENT_ID,
    "client_secret": CLIENT_SECRET,
    "redirect_uri": REDIRECT_URI,
})
check("授权码重放被拒", status == 400 and not rsp.get("success"), str(rsp))

# 5. 用户信息
status, rsp = request("/oauth/userinfo", token=access)
check("GET /oauth/userinfo 通过", status == 200 and rsp.get("success"), str(rsp))
check("userinfo 带回 pp 号", bool(rsp.get("ppId")), str(rsp))

# 5b. 错误令牌必须 401
status, rsp = request("/oauth/userinfo", token="deadbeef")
check("无效 access_token 被拒", status == 401, f"status={status}")

# 6. 错误 client_secret
status, rsp2 = request("/oauth/authorize", "POST", {
    "client_id": CLIENT_ID,
    "redirect_uri": REDIRECT_URI,
    "state": "s2",
}, token=token)
code2 = (urllib.parse.parse_qs(urllib.parse.urlparse(rsp2.get("redirect", "")).query).get("code") or [""])[0]
status, rsp = request("/oauth/token", "POST", form={
    "grant_type": "authorization_code",
    "code": code2,
    "client_id": CLIENT_ID,
    "client_secret": "wrong-secret",
    "redirect_uri": REDIRECT_URI,
})
check("错误 client_secret 被拒", status == 400 and not rsp.get("success"), str(rsp))

print(f"\n结果：{passed} 通过 / {failed} 失败")
sys.exit(0 if failed == 0 else 1)
