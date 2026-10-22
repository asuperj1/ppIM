"""往 pp 的 sqlite 里塞一个测试用户（密码 test1234）。"""
from __future__ import annotations

import hashlib
import os
import sqlite3
import sys
import time

DB = sys.argv[1] if len(sys.argv) > 1 else r"E:\myIM\lab\10_oauth\test.db"
USERNAME = sys.argv[2] if len(sys.argv) > 2 else "oauthuser"
NICKNAME = sys.argv[3] if len(sys.argv) > 3 else "OAuth 测试号"
PASSWORD = sys.argv[4] if len(sys.argv) > 4 else "test1234"

salt = os.urandom(16).hex()
pwd_hash = hashlib.sha256((salt + PASSWORD).encode()).hexdigest()

conn = sqlite3.connect(DB)
cur = conn.cursor()
cur.execute("SELECT uid FROM users WHERE username = ?", (USERNAME,))
row = cur.fetchone()

if row:
    uid = row[0]
    cur.execute("UPDATE users SET salt = ?, password_hash = ? WHERE uid = ?", (salt, pwd_hash, uid))
else:
    cur.execute(
        "INSERT INTO users(username, nickname, pp_id, signature, avatar_url, salt, password_hash, created_at) "
        "VALUES(?, ?, '', '', '', ?, ?, ?)",
        (USERNAME, NICKNAME, salt, pwd_hash, int(time.time())),
    )
    uid = cur.lastrowid

pp_id = "pp" + str(100000 + uid)
cur.execute("UPDATE users SET pp_id = ? WHERE uid = ? AND (pp_id IS NULL OR pp_id = '')", (pp_id, uid))
conn.commit()
print(f"uid={uid} username={USERNAME} pp_id={pp_id} password={PASSWORD}")
conn.close()
