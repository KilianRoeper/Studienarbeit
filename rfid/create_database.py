"""
module for creating a database 
"""

import sqlite3

conn = sqlite3.connect("rfid_data.db")
cursor = conn.cursor()

cursor.execute("""
CREATE TABLE IF NOT EXISTS products (
    uid TEXT PRIMARY KEY,
    current TEXT, 
    timestamp TEXT,
    finished BOOLEAN
);
""")

cursor.execute("""
CREATE TABLE IF NOT EXISTS assembling (
    uid TEXT PRIMARY KEY,
    state TEXT DEFAULT NULL,
    timestamp TEXT DEFAULT NULL,
    FOREIGN KEY (uid) REFERENCES products(uid) ON DELETE CASCADE
);
""")

cursor.execute("""
CREATE TABLE IF NOT EXISTS shelf (
    uid TEXT PRIMARY KEY,
    state TEXT DEFAULT NULL,
    timestamp TEXT DEFAULT NULL,
    in_shelf BOOLEAN DEFAULT NULL,
    position INTEGER DEFAULT NULL,
    FOREIGN KEY (uid) REFERENCES products(uid) ON DELETE CASCADE
);
""")

cursor.execute("""
CREATE TABLE IF NOT EXISTS screwing (
    uid TEXT PRIMARY KEY,
    state TEXT DEFAULT NULL,
    timestamp TEXT DEFAULT NULL,
    FOREIGN KEY (uid) REFERENCES products(uid) ON DELETE CASCADE
);
""")

cursor.execute("""
CREATE TABLE IF NOT EXISTS end_of_line (
    uid TEXT PRIMARY KEY,
    state TEXT DEFAULT NULL,
    timestamp TEXT DEFAULT NULL,
    FOREIGN KEY (uid) REFERENCES products(uid) ON DELETE CASCADE
);
""")
