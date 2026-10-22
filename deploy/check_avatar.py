"""列出用户的昵称与头像地址，用于验证头像修改是否落库。"""

import sqlite3

conn = sqlite3.connect("/opt/pp/data/im.db")
rows = conn.execute(
    "SELECT uid, nickname, avatar_url FROM users WHERE uid IN (36, 37)"
).fetchall()
for uid, nickname, avatar in rows:
    print(uid, nickname, avatar or "(空)")
