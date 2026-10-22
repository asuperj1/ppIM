import sqlite3

conn = sqlite3.connect("/opt/pp/data/im.db")
total = conn.execute("select count(*) from users").fetchone()[0]
print("pp users:", total)
for pp_id, name in conn.execute("select pp_id, username from users order by uid"):
    print("  ", pp_id, name)
conn.close()
