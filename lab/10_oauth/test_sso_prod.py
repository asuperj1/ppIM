"""pp -> 校捷通 单点登录端到端联调（生产环境，纯 HTTP/WS，不需要浏览器）。

流程：
  1. 在 pp 注册一个临时账号（WS REGISTER_REQ）
  2. 校捷通 /auth/pp/start 拿 state（不跟随 302）
  3. pp /api/login 拿会话令牌
  4. pp /oauth/authorize 拿一次性 code
  5. 校捷通 /auth/pp/callback 换 ticket（不跟随 302）
  6. 校捷通 /auth/pp/exchange 用 ticket 换校捷通 JWT
  7. 用 JWT 访问 /api/v1/user/me
  8. 重复用同一个 ticket 必须失败

用法：python test_sso_prod.py
"""

from __future__ import annotations

import asyncio
import json
import secrets
import urllib.error
import urllib.parse
import urllib.request

import websockets

PP = "https://pp.xiaojietong.online"
XJT = "https://xiaojietong.online"
CLIENT_ID = "xiaojietong-web"


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


OPENER = urllib.request.build_opener(NoRedirect)


def http(url: str, method: str = "GET", body: dict | None = None,
         token: str = "", form: dict | None = None) -> tuple[int, dict, dict]:
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

    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with OPENER.open(req, timeout=15) as rsp:
            raw = rsp.read().decode()
            return rsp.status, (json.loads(raw) if raw.strip().startswith("{") else {}), dict(rsp.headers)
    except urllib.error.HTTPError as err:
        raw = err.read().decode()
        try:
            payload = json.loads(raw) if raw.strip().startswith("{") else {}
        except ValueError:
            payload = {}
        return err.code, payload, dict(err.headers)


async def pp_register(username: str, password: str, nickname: str) -> dict:
    async with websockets.connect("wss://pp.xiaojietong.online/ws", open_timeout=10) as ws:
        payload = {"username": username, "password": password, "nickname": nickname}
        await ws.send(json.dumps({"type": 5, "body": json.dumps(payload)}))
        for _ in range(8):
            raw = await asyncio.wait_for(ws.recv(), timeout=10)
            msg = json.loads(raw)
            if int(msg.get("type", 0)) == 6:
                return json.loads(msg.get("body") or "{}")
    raise RuntimeError("register timeout")


def main() -> int:
    suffix = secrets.token_hex(3)
    username = "ssotest" + suffix
    password = "sso-" + suffix
    nickname = "SSO test " + suffix

    print("== 1. register temp account on pp ==")
    reg = asyncio.run(pp_register(username, password, nickname))
    if not reg.get("success"):
        print("  register failed:", reg)
        return 1
    print(f"  uid={reg.get('uid')} pp_id={reg.get('ppId')} username={username}")

    print("== 2. xjt /auth/pp/start ==")
    status, _, headers = http(XJT + "/api/v1/auth/pp/start?next=/")
    location = headers.get("Location", "")
    if status not in (301, 302, 303, 307) or "state=" not in location:
        print("  start failed:", status, location, headers)
        return 1
    state = urllib.parse.parse_qs(urllib.parse.urlparse(location).query)["state"][0]
    print("  302 ->", location.split("?")[0], "state len", len(state))

    print("== 3. pp /api/login ==")
    status, rsp, _ = http(PP + "/api/login", "POST",
                          {"username": username, "password": password})
    if status != 200 or not rsp.get("token"):
        print("  login failed:", status, rsp)
        return 1
    pp_token = rsp["token"]
    print("  session token ok")

    print("== 4. pp /oauth/authorize ==")
    redirect_uri = XJT + "/api/v1/auth/pp/callback"
    status, rsp, _ = http(PP + "/oauth/authorize", "POST", {
        "client_id": CLIENT_ID,
        "redirect_uri": redirect_uri,
        "state": state,
    }, token=pp_token)
    if status != 200 or not rsp.get("success"):
        print("  authorize failed:", status, rsp)
        return 1
    code = urllib.parse.parse_qs(urllib.parse.urlparse(rsp["redirect"]).query)["code"][0]
    print("  code len", len(code))

    print("== 5. xjt /auth/pp/callback ==")
    callback = redirect_uri + "?" + urllib.parse.urlencode({"code": code, "state": state})
    status, _, headers = http(callback)
    location = headers.get("Location", "")
    if status not in (301, 302, 303, 307) or "pp_ticket=" not in location:
        print("  callback failed:", status, location)
        return 1
    ticket = urllib.parse.parse_qs(urllib.parse.urlparse(location).query)["pp_ticket"][0]
    print("  302 ->", location.split("?")[0], "ticket len", len(ticket))

    print("== 6. /auth/pp/exchange ==")
    status, rsp, _ = http(XJT + "/api/v1/auth/pp/exchange", "POST", {"ticket": ticket})
    if status != 200 or rsp.get("code") != 0 or not rsp.get("data", {}).get("token"):
        print("  exchange failed:", status, rsp)
        return 1
    data = rsp["data"]
    print("  jwt ok, user =", json.dumps(data["user"], ensure_ascii=False))

    print("== 7. jwt -> /api/v1/user/me ==")
    status, rsp, _ = http(XJT + "/api/v1/user/me", token=data["token"])
    print("  /user/me ->", status, json.dumps(rsp, ensure_ascii=False)[:300])

    print("== 8. replay same ticket ==")
    status, rsp, _ = http(XJT + "/api/v1/auth/pp/exchange", "POST", {"ticket": ticket})
    print("  second exchange ->", status, json.dumps(rsp, ensure_ascii=False)[:160])

    print(f"\ntemp account to clean: pp username={username}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
