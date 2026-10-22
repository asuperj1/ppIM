"""线上验证：首次设置密码（小程序 / pp 用户场景）。

  A. 纯 pp 用户不传手机号 → 必须拒绝（否则密码没有可用账号）
  B. 传手机号 → 设置成功，随后可用「手机号 + 密码」登录
  C. 设置密码后 pp_id 仍然保留（不误解绑）
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
REDIRECT_URI = XJT + "/api/v1/auth/pp/callback"

passed = 0
failed = 0


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


OPENER = urllib.request.build_opener(NoRedirect)


def http(url, method="GET", body=None, token="", form=None):
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


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  [PASS] {name}")
    else:
        failed += 1
        print(f"  [FAIL] {name} {detail}")


async def register_pp(username, password, nickname):
    async with websockets.connect("wss://pp.xiaojietong.online/ws", open_timeout=10) as ws:
        await ws.send(json.dumps({"type": 5, "body": json.dumps(
            {"username": username, "password": password, "nickname": nickname})}))
        for _ in range(8):
            msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=10))
            if int(msg.get("type", 0)) == 6:
                return json.loads(msg.get("body") or "{}")
    raise RuntimeError("timeout")


def new_pp_user(suffix, tag):
    """注册 pp 账号 -> 登录 -> 走校捷通 SSO -> 返回校捷通 JWT。"""
    username = "ssopw" + tag + suffix
    password = "pp-" + suffix
    reg = asyncio.run(register_pp(username, password, "密码测试" + tag))
    assert reg.get("success"), reg

    status, rsp, _ = http(PP + "/api/login", "POST",
                          {"username": username, "password": password})
    pp_token = rsp.get("token", "")
    assert pp_token, rsp

    status, _, headers = http(XJT + "/api/v1/auth/pp/start?next=/")
    state = urllib.parse.parse_qs(urllib.parse.urlparse(headers["Location"]).query)["state"][0]

    status, rsp, _ = http(PP + "/oauth/authorize", "POST", {
        "client_id": CLIENT_ID, "redirect_uri": REDIRECT_URI, "state": state, "scope": "profile",
    }, token=pp_token)
    code = urllib.parse.parse_qs(urllib.parse.urlparse(rsp["redirect"]).query)["code"][0]

    status, _, headers = http(REDIRECT_URI + "?" + urllib.parse.urlencode({"code": code, "state": state}))
    ticket = urllib.parse.parse_qs(urllib.parse.urlparse(headers["Location"]).query)["pp_ticket"][0]

    status, rsp, _ = http(XJT + "/api/v1/auth/pp/exchange", "POST", {"ticket": ticket})
    data = rsp.get("data", {})
    return username, data.get("token", ""), data.get("user", {}).get("id")


def main():
    suffix = secrets.token_hex(3)
    phone = "198" + "".join(secrets.choice("0123456789") for _ in range(8))
    pwd = "newpw-" + suffix

    print("== A. 不传手机号必须被拒绝 ==")
    user_a, token_a, uid_a = new_pp_user(suffix, "a")
    check("纯 pp 用户拿到 JWT", bool(token_a), f"uid={uid_a}")

    status, rsp, _ = http(XJT + "/api/v1/auth/password", "POST", {"password": pwd}, token=token_a)
    check("缺少手机号被拒绝", status == 400 and rsp.get("code") == 1001, str(rsp))

    print("== B. 传手机号后设置成功并可登录 ==")
    status, rsp, _ = http(XJT + "/api/v1/auth/password", "POST",
                          {"password": pwd, "phone": phone}, token=token_a)
    check("设置密码成功", status == 200 and rsp.get("code") == 0, str(rsp))
    new_token = rsp.get("data", {}).get("token", "")
    check("返回新 token（旧 token 已失效）", bool(new_token), str(rsp)[:120])

    status, rsp, _ = http(XJT + "/api/v1/user/me", token=token_a)
    check("旧 token 已失效", status == 401, f"status={status}")

    status, rsp, _ = http(XJT + "/api/v1/auth/login", "POST",
                          {"account": phone, "password": pwd})
    check("手机号 + 密码可登录", status == 200 and rsp.get("code") == 0, str(rsp))

    status, rsp, _ = http(XJT + "/api/v1/user/me", token=new_token)
    me = rsp.get("data", {})
    check("设置密码后 pp_id 保留", bool(me.get("pp_id")), str(rsp))
    check("手机号已写入", me.get("phone") == phone, str(me))

    print("== C. 重复手机号被拒绝 ==")
    user_c, token_c, uid_c = new_pp_user(suffix, "c")
    status, rsp, _ = http(XJT + "/api/v1/auth/password", "POST",
                          {"password": pwd, "phone": phone}, token=token_c)
    check("手机号被占用返回 3002", status == 400 and rsp.get("code") == 3002, str(rsp))

    print("\n临时账号（清理用）：")
    print(f"  xjt phone={phone} uid_a={uid_a} uid_c={uid_c}")
    print(f"  pp username={user_a}, {user_c}")
    print(f"\n结果：{passed} 通过 / {failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
