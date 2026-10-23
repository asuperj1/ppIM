"""把演示图片上传到 COS，输出 file_url 映射（JSON）。

在服务器上运行（需要读 /opt/pp/config/cos.env）：

    python3 upload_demo_images.py /tmp/demo-images
"""

import hashlib
import hmac
import json
import os
import sys
import time
import urllib.request
import uuid

ENV_FILE = "/opt/pp/config/cos.env"


def load_env(path):
    env = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            env[key.strip()] = value.strip().strip('"').strip("'")
    return env


def sign_put(secret_id, secret_key, host, key, expire=1800):
    now = int(time.time())
    key_time = f"{now};{now + expire}"
    sign_key = hmac.new(secret_key.encode(), key_time.encode(), hashlib.sha1).hexdigest()
    http_string = f"put\n/{key}\n\nhost={host}\n"
    string_to_sign = ("sha1\n" + key_time + "\n"
                      + hashlib.sha1(http_string.encode()).hexdigest() + "\n")
    signature = hmac.new(sign_key.encode(), string_to_sign.encode(),
                         hashlib.sha1).hexdigest()
    url = (f"https://{host}/{key}"
           f"?q-sign-algorithm=sha1&q-ak={secret_id}"
           f"&q-sign-time={key_time}&q-key-time={key_time}"
           f"&q-header-list=host&q-url-param-list=&q-signature={signature}")
    return url, f"https://{host}/{key}"


def main():
    src_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/demo-images"
    env = load_env(ENV_FILE)
    secret_id = env["COS_SECRET_ID"]
    secret_key = env["COS_SECRET_KEY"]
    # 与 cos.cpp 的 CosConfig::fromEnv 一致：COS_HOST 留空时用 bucket + region 拼
    host = env.get("COS_HOST", "").replace("https://", "").rstrip("/")
    if not host:
        host = f'{env["COS_BUCKET"]}.cos.{env["COS_REGION"]}.myqcloud.com'

    day = time.strftime("%Y%m%d")
    result = {}
    for name in sorted(os.listdir(src_dir)):
        if not name.lower().endswith((".jpg", ".jpeg", ".png", ".webp")):
            continue
        path = os.path.join(src_dir, name)
        with open(path, "rb") as fh:
            data = fh.read()
        key = f"chat/{day}/{uuid.uuid4().hex}.jpg"
        url, file_url = sign_put(secret_id, secret_key, host, key)
        req = urllib.request.Request(url, data=data, method="PUT",
                                     headers={"Content-Type": "image/jpeg"})
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                code = resp.status
        except Exception as exc:  # noqa: BLE001
            print("FAIL", name, exc, file=sys.stderr)
            continue
        print(f"{name} -> HTTP {code} {file_url}", flush=True)
        result[name] = file_url

    with open("/tmp/demo_image_urls.json", "w", encoding="utf-8") as fh:
        json.dump(result, fh, ensure_ascii=False, indent=2)
    print("saved /tmp/demo_image_urls.json")


if __name__ == "__main__":
    main()
