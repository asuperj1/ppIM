"""清理 SSO 联调临时账号（pp + 校捷通）。"""
import os
import sqlite3
import subprocess

ENV_PATH = "/opt/xiaojietong/shared/server.env"

conn = sqlite3.connect("/opt/pp/data/im.db")
cur = conn.cursor()
cur.execute("SELECT uid, username FROM users WHERE username LIKE 'ssotest%' OR username LIKE 'ssoui%'")
rows = cur.fetchall()
print("pp temp users:", rows)
uids = [int(r[0]) for r in rows]
for uid in uids:
    cur.execute("DELETE FROM auth_tokens WHERE uid = ?", (uid,))
    cur.execute("DELETE FROM users WHERE uid = ?", (uid,))
conn.commit()
conn.close()

if not uids:
    print("nothing to clean")
    raise SystemExit(0)

env = {}
with open(ENV_PATH, encoding="utf-8") as fh:
    for line in fh:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        env[key.strip()] = value.strip().strip('"').strip("'")

id_list = ",".join(str(u) for u in uids)
sql = f"DELETE FROM `user` WHERE pp_uid IN ({id_list}); SELECT ROW_COUNT() AS deleted;"

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
