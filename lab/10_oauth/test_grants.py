"""pp 授权管理联调：列表 / 解除 / 撤销令牌立即失效。"""
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


print(f"== pp 授权管理联调 @ {BASE} ==")

status, rsp = request("/api/login", "POST", {"username": USERNAME, "password": PASSWORD})
check("登录", status == 200 and bool(rsp.get("token")), str(rsp))
session = rsp.get("token", "")
uid = int(rsp.get("uid") or 0)

status, rsp = request("/oauth/authorize", "POST", {
    "client_id": CLIENT_ID, "redirect_uri": REDIRECT_URI, "state": "g1",
}, token=session)
code = urllib.parse.parse_qs(urllib.parse.urlparse(rsp.get("redirect", "")).query).get("code", [""])[0]
check("授权拿 code", status == 200 and bool(code), str(rsp))

status, rsp = request("/oauth/token", "POST", form={
    "grant_type": "authorization_code", "code": code, "client_id": CLIENT_ID,
    "client_secret": CLIENT_SECRET, "redirect_uri": REDIRECT_URI,
})
access = rsp.get("accessToken", "")
check("换到 access_token", status == 200 and bool(access), str(rsp))

status, rsp = request("/api/grants", token=session)
grants = rsp.get("grants") or []
check("授权列表包含该应用", status == 200 and any(g.get("clientId") == CLIENT_ID for g in grants), str(rsp))
item = next((g for g in grants if g.get("clientId") == CLIENT_ID), {})
check("列表带应用名", bool(item.get("name")), str(item))
check("列表带授权范围 scope", item.get("scope") == "profile", str(item))
check("标记为已授权", int(item.get("activeTokens", 0)) >= 1, str(item))

status, rsp = request("/oauth/userinfo", token=access)
check("撤销前 userinfo 可用", status == 200 and rsp.get("success"), str(rsp))

status, rsp = request("/api/grants/revoke", "POST", {"clientId": CLIENT_ID}, token=session)
check("解除授权返回成功", status == 200 and rsp.get("success"), str(rsp))
check("删除了至少 1 个令牌", int(rsp.get("revoked", 0)) >= 1, str(rsp.get("revoked")))

status, rsp = request("/oauth/userinfo", token=access)
check("撤销后 access_token 立即失效", status == 401, f"status={status}")

status, rsp = request("/api/grants", token=session)
check("列表已清空该应用", all(g.get("clientId") != CLIENT_ID for g in rsp.get("grants") or []), str(rsp))

status, rsp = request("/api/grants")
check("未登录访问列表被拒", status == 401, f"status={status}")

# 9. 接入方服务端撤销（校捷通解绑 pp 时联动）
status, rsp = request("/oauth/authorize", "POST", {
    "client_id": CLIENT_ID, "redirect_uri": REDIRECT_URI, "state": "g3",
}, token=session)
code3 = urllib.parse.parse_qs(urllib.parse.urlparse(rsp.get("redirect", "")).query).get("code", [""])[0]
status, rsp = request("/oauth/token", "POST", form={
    "grant_type": "authorization_code", "code": code3, "client_id": CLIENT_ID,
    "client_secret": CLIENT_SECRET, "redirect_uri": REDIRECT_URI,
})
access3 = rsp.get("accessToken", "")
check("重新授权拿到 access_token", bool(access3), str(rsp))

status, rsp = request("/oauth/revoke", "POST", form={
    "client_id": CLIENT_ID, "client_secret": CLIENT_SECRET, "uid": uid,
})
check("接入方服务端撤销成功", status == 200 and rsp.get("success"), str(rsp))
check("撤销删除至少 1 个令牌", int(rsp.get("revoked", 0)) >= 1, str(rsp.get("revoked")))

status, rsp = request("/oauth/userinfo", token=access3)
check("撤销后该令牌立即失效", status == 401, f"status={status}")

status, rsp = request("/oauth/revoke", "POST", form={
    "client_id": CLIENT_ID, "client_secret": "wrong", "uid": uid,
})
check("错误 secret 无法撤销", status == 400 and not rsp.get("success"), str(rsp))

print(f"\n结果：{passed} 通过 / {failed} 失败")
sys.exit(0 if failed == 0 else 1)
