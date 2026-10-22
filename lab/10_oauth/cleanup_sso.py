"""清理 SSO 联调产生的临时账号（服务器端执行）。

pp 侧：删除用户名以 ssotest 开头的用户及其 token
校捷通：删除没有任何身份的测试用户（openid 为空、无 pp_uid、昵称以 "SSO test" 开头）
"""
import os
import sqlite3
import subprocess

PP_DB = "/opt/pp/data/im.db"
ENV_PATH = "/opt/xiaojietong/shared/server.env"

conn = sqlite3.connect(PP_DB)
cur = conn.cursor()
cur.execute("SELECT uid, username FROM users WHERE username LIKE 'ssotest%'")
rows = cur.fetchall()
print("pp temp users:", rows)
uids = [r[0] for r in rows]
for uid in uids:
    cur.execute("DELETE FROM auth_tokens WHERE uid = ?", (uid,))
    cur.execute("DELETE FROM users WHERE uid = ?", (uid,))
conn.commit()
conn.close()
print("pp cleaned:", len(uids))

env = {}
with open(ENV_PATH, encoding="utf-8") as fh:
    for line in fh:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        env[key.strip()] = value.strip().strip('"').strip("'")

sql = (
    "DELETE FROM `user` "
    "WHERE openid IS NULL AND pp_uid IS NULL AND nickname LIKE 'SSO test%';"
    "SELECT ROW_COUNT() AS deleted;"
)
proc = subprocess.run(
    [
        "mysql",
        "-h", env.get("XJT_DB_HOST", "127.0.0.1"),
        "-P", env.get("XJT_DB_PORT", "3306"),
        "-u", env.get("XJT_DB_USER", "root"),
        env.get("XJT_DB_NAME", "xiaojietong"),
        "-e", sql,
    ],
    env={**os.environ, "MYSQL_PWD": env.get("XJT_DB_PASSWORD", "")},
    capture_output=True,
)
print("xjt rc:", proc.returncode)
print(proc.stdout.decode("utf-8", "replace"))
print(proc.stderr.decode("utf-8", "replace"))
