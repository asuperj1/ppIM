import sqlite3

conn = sqlite3.connect("/opt/pp/data/im.db")
cur = conn.execute("DELETE FROM favorites WHERE uid = 36")
conn.commit()
print("removed rows:", cur.rowcount)
for row in conn.execute("SELECT uid, msg_id, content FROM favorites LIMIT 5"):
    print(row)
conn.close()
