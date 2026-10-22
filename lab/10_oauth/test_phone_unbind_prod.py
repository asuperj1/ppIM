"""线上验证：手机号换绑 + 解绑联动撤销 pp 授权。"""
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
            return rsp.status, (json.loads(raw) if raw.strip().startswith("{") else {})
    except urllib.error.HTTPError as err:
        raw = err.read().decode()
        try:
            payload = json.loads(raw) if raw.strip().startswith("{") else {}
        except ValueError:
            payload = {}
        return err.code, payload


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


def rand_phone(prefix="197"):
    return prefix + "".join(secrets.choice("0123456789") for _ in range(8))


def main():
    suffix = secrets.token_hex(3)
    phone1 = rand_phone()
    phone2 = rand_phone()
    pwd = "pw-" + suffix

    print("== 1. 注册本地账号 ==")
    status, rsp = http(XJT + "/api/v1/auth/register", "POST", {
        "phone": phone1, "password": pwd, "nickname": "手机号测试"
    })
    check("注册成功", status == 200 and rsp.get("code") == 0, str(rsp))
    token = rsp.get("data", {}).get("token", "")

    print("== 2. 换绑手机号 ==")
    status, rsp = http(XJT + "/api/v1/user/phone", "POST", {"phone": phone2}, token=token)
    check("换绑成功", status == 200 and rsp.get("code") == 0, str(rsp))
    check("返回新手机号", rsp.get("data", {}).get("phone") == phone2, str(rsp))

    status, rsp = http(XJT + "/api/v1/auth/login", "POST", {"account": phone2, "password": pwd})
    check("新手机号可登录", status == 200 and rsp.get("code") == 0, str(rsp))

    status, rsp = http(XJT + "/api/v1/auth/login", "POST", {"account": phone1, "password": pwd})
    check("旧手机号不能再登录", status == 400, f"status={status}")

    status, rsp = http(XJT + "/api/v1/user/phone", "POST", {"phone": "12345"}, token=token)
    check("非法手机号被拒", status == 400 and rsp.get("code") == 1001, str(rsp))

    print("== 3. 绑定 pp 并确认 pp 侧有授权记录 ==")
    status, rsp = http(XJT + "/api/v1/auth/pp/bind-start", "POST", {}, token=token)
    bind_url = rsp.get("data", {}).get("url", "")
    state = urllib.parse.parse_qs(urllib.parse.urlparse(bind_url).query)["state"][0]

    pp_user = "ssophone" + suffix
    pp_pwd = "pp-" + suffix
    reg = asyncio.run(register_pp(pp_user, pp_pwd, "手机号测试号"))
    check("注册 pp 账号", bool(reg.get("success")), str(reg))
    status, rsp = http(PP + "/api/login", "POST", {"username": pp_user, "password": pp_pwd})
    pp_token = rsp.get("token", "")
    check("pp 登录", bool(pp_token))

    status, rsp = http(PP + "/oauth/authorize", "POST", {
        "client_id": CLIENT_ID, "redirect_uri": REDIRECT_URI, "state": state, "scope": "profile",
    }, token=pp_token)
    code = urllib.parse.parse_qs(urllib.parse.urlparse(rsp.get("redirect", "")).query).get("code", [""])[0]
    check("pp 授权", bool(code))

    status, _ = http(REDIRECT_URI + "?" + urllib.parse.urlencode({"code": code, "state": state}))
    check("绑定回调", status in (301, 302, 303, 307), f"status={status}")

    status, rsp = http(PP + "/api/grants", token=pp_token)
    grants = rsp.get("grants") or []
    check("pp 授权列表含校捷通", any(g.get("clientId") == CLIENT_ID for g in grants), str(rsp))

    print("== 4. 解绑 pp 应联动撤销授权 ==")
    status, rsp = http(XJT + "/api/v1/auth/pp/unbind", "POST", {}, token=token)
    check("解绑成功", status == 200 and rsp.get("code") == 0, str(rsp))

    status, rsp = http(PP + "/api/grants", token=pp_token)
    grants = rsp.get("grants") or []
    check("pp 授权记录已清除", all(g.get("clientId") != CLIENT_ID for g in grants), str(rsp))

    print("\n临时账号（清理用）：")
    print(f"  xjt phone={phone1} -> {phone2}")
    print(f"  pp username={pp_user}")
    print(f"\n结果：{passed} 通过 / {failed} 失败")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
