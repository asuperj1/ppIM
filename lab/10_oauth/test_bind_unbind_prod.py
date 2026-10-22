"""线上验证：pp 绑定 / 解绑 全部分支。

覆盖：
  A. 本地账号（有密码）绑定 pp → 解绑成功 → 再解绑返回 3003
  B. 纯 pp 账号（无 openid、无密码）解绑 → 必须被拒绝（3004）
  C. /user/me 返回 pp_id

脚本会创建临时账号，结束后请用 cleanup 脚本清理。
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


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


OPENER = urllib.request.build_opener(NoRedirect)
passed = 0
failed = 0


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


async def register_pp(username: str, password: str, nickname: str):
    async with websockets.connect("wss://pp.xiaojietong.online/ws", open_timeout=10) as ws:
        await ws.send(json.dumps({"type": 5, "body": json.dumps(
            {"username": username, "password": password, "nickname": nickname})}))
        for _ in range(8):
            msg = json.loads(await asyncio.wait_for(ws.recv(), timeout=10))
            if int(msg.get("type", 0)) == 6:
                return json.loads(msg.get("body") or "{}")
    raise RuntimeError("register timeout")


def pp_login(username, password):
    status, rsp, _ = http(PP + "/api/login", "POST",
                          {"username": username, "password": password})
    return rsp.get("token", "") if status == 200 else ""


def pp_authorize(pp_token, state, scope="profile"):
    status, rsp, _ = http(PP + "/oauth/authorize", "POST", {
        "client_id": CLIENT_ID,
        "redirect_uri": REDIRECT_URI,
        "state": state,
        "scope": scope,
    }, token=pp_token)
    if status != 200 or not rsp.get("success"):
        return ""
    return urllib.parse.parse_qs(urllib.parse.urlparse(rsp["redirect"]).query)["code"][0]


def main():
    suffix = secrets.token_hex(3)
    phone = "199" + "".join(secrets.choice("0123456789") for _ in range(8))
    password = "pw-" + suffix

    print("== A. 本地账号绑定 / 解绑 ==")
    status, rsp, _ = http(XJT + "/api/v1/auth/register", "POST", {
        "phone": phone, "password": password, "nickname": "绑定解绑测试"
    })
    check("注册本地账号", status == 200 and rsp.get("code") == 0, str(rsp))
    token = rsp.get("data", {}).get("token", "")
    uid = rsp.get("data", {}).get("user", {}).get("id")

    status, rsp, _ = http(XJT + "/api/v1/auth/pp/bind-start", "POST", {}, token=token)
    bind_url = rsp.get("data", {}).get("url", "")
    check("bind-start 返回授权地址", status == 200 and "state=" in bind_url, str(rsp))
    state = urllib.parse.parse_qs(urllib.parse.urlparse(bind_url).query)["state"][0]
    check("授权地址带 scope=profile", "scope=profile" in bind_url, bind_url)

    pp_user = "ssounbind" + suffix
    pp_pwd = "pp-" + suffix
    reg = asyncio.run(register_pp(pp_user, pp_pwd, "解绑测试号"))
    check("注册 pp 测试账号", bool(reg.get("success")), str(reg))
    pp_token = pp_login(pp_user, pp_pwd)
    check("pp 登录", bool(pp_token))

    code = pp_authorize(pp_token, state)
    check("pp 授权拿 code", bool(code))

    status, _, headers = http(REDIRECT_URI + "?" + urllib.parse.urlencode({"code": code, "state": state}))
    location = headers.get("Location", "")
    check("绑定回调成功", status in (301, 302, 303, 307) and "pp_bind=ok" in location, location)

    status, rsp, _ = http(XJT + "/api/v1/user/me", token=token)
    bound_pp_id = rsp.get("data", {}).get("pp_id", "")
    check("/user/me 返回已绑定 pp_id", bool(bound_pp_id), str(rsp))

    status, rsp, _ = http(XJT + "/api/v1/auth/pp/unbind", "POST", {}, token=token)
    check("解绑成功", status == 200 and rsp.get("code") == 0, str(rsp))

    status, rsp, _ = http(XJT + "/api/v1/user/me", token=token)
    check("解绑后 pp_id 为空", rsp.get("data", {}).get("pp_id", "x") == "", str(rsp))

    status, rsp, _ = http(XJT + "/api/v1/auth/pp/unbind", "POST", {}, token=token)
    check("重复解绑返回 3003", status == 400 and rsp.get("code") == 3003, str(rsp))

    print("== B. 纯 pp 账号不能解绑（会锁死自己）==")
    status, _, headers = http(XJT + "/api/v1/auth/pp/start?next=/")
    login_state = urllib.parse.parse_qs(urllib.parse.urlparse(headers.get("Location", "")).query)["state"][0]
    pure_pp = "ssopure" + suffix
    asyncio.run(register_pp(pure_pp, pp_pwd, "纯 pp 用户"))
    pure_token = pp_login(pure_pp, pp_pwd)
    pure_code = pp_authorize(pure_token, login_state)
    status, _, headers = http(REDIRECT_URI + "?" + urllib.parse.urlencode(
        {"code": pure_code, "state": login_state}))
    ticket = urllib.parse.parse_qs(urllib.parse.urlparse(headers.get("Location", "")).query).get("pp_ticket", [""])[0]
    check("纯 pp 用户拿到 ticket", bool(ticket))

    status, rsp, _ = http(XJT + "/api/v1/auth/pp/exchange", "POST", {"ticket": ticket})
    pure_jwt = rsp.get("data", {}).get("token", "")
    pure_uid = rsp.get("data", {}).get("user", {}).get("id")
    check("纯 pp 用户换到 JWT", bool(pure_jwt), str(rsp))

    status, rsp, _ = http(XJT + "/api/v1/auth/pp/unbind", "POST", {}, token=pure_jwt)
    check("纯 pp 用户解绑被拒绝 3004", status == 400 and rsp.get("code") == 3004, str(rsp))

    print("\n临时账号（清理用）：")
    print(f"  xjt phone={phone} uid={uid} pure_uid={pure_uid}")
    print(f"  pp username={pp_user}, {pure_pp}")
    print(f"\n结果：{passed} 通过 / {failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
