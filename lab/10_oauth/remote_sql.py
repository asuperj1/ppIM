"""远程执行 21_user_pp.sql（在服务器上用 sudo python3 运行）。"""
import os
import subprocess

ENV_PATH = "/opt/xiaojietong/shared/server.env"
SQL_PATH = "/opt/xiaojietong/app/db/sql/21_user_pp.sql"

env = {}
with open(ENV_PATH, encoding="utf-8") as fh:
    for line in fh:
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        env[key.strip()] = value.strip().strip('"').strip("'")

cmd = [
    "mysql",
    "-h", env.get("XJT_DB_HOST", "127.0.0.1"),
    "-P", env.get("XJT_DB_PORT", "3306"),
    "-u", env.get("XJT_DB_USER", "root"),
    env.get("XJT_DB_NAME", "xiaojietong"),
]

with open(SQL_PATH, "rb") as fh:
    sql = fh.read()

proc = subprocess.run(
    cmd,
    input=sql,
    env={**os.environ, "MYSQL_PWD": env.get("XJT_DB_PASSWORD", "")},
    capture_output=True,
)
print("rc =", proc.returncode)
print(proc.stdout.decode("utf-8", "replace")[-800:])
print(proc.stderr.decode("utf-8", "replace")[-800:])

# 打印结果结构，便于确认
check = subprocess.run(
    cmd + ["-e", "SHOW COLUMNS FROM `user` LIKE 'pp_%'; SHOW INDEX FROM `user` WHERE Key_name IN ('uk_pp_uid','idx_phone');"],
    env={**os.environ, "MYSQL_PWD": env.get("XJT_DB_PASSWORD", "")},
    capture_output=True,
)
print(check.stdout.decode("utf-8", "replace"))
print(check.stderr.decode("utf-8", "replace"))
