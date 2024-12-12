"""
module to maintain the database on received data
"""

import sqlite3
from flask import Flask, request, jsonify
from datetime import datetime



DB_NAME = "rfid_data.db"
app = Flask(__name__)


def initialize_uid(uid):
    """insert new UIDs to all database tables 

    Args:
        uid (bytestring): uid of the nfc tag
    """
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()

    # insert uid to main table if it doesn't exist yet
    cursor.execute("INSERT OR IGNORE INTO products (uid) VALUES (?)", (uid,))

    # insert uid to all station specific stations
    cursor.execute("INSERT OR IGNORE INTO assembling (uid) VALUES (?)", (uid,))
    cursor.execute("INSERT OR IGNORE INTO shelf (uid) VALUES (?)", (uid,))
    cursor.execute("INSERT OR IGNORE INTO screwing (uid) VALUES (?)", (uid,))
    cursor.execute("INSERT OR IGNORE INTO end_of_line (uid) VALUES (?)", (uid,))

    conn.commit()
    conn.close()


def update_station_data(data, timestamp):
    """update database with received data
    
    Args:
        data (json): data to write to database from stations
    """
    conn = sqlite3.connect(DB_NAME)
    cursor = conn.cursor()    
    
    uid_hex = data.get("UID")  
    uid = "".join(uid_hex)    
    if uid == "00000000":
        print("no valid uid")
    else:
        initialize_uid(uid)
    
    
    current_state_mapping = {
            0x01: "to_shelf",
            0x02: "at_shelf",
            0x03: "in_shelf",
            0x04: "to_screwing",
            0x05: "at_screwing",
            0x06: "to_eol",
            0x07: "at_eol",
            0x08: "at_assembling"
    }
    stations_mapping = {
            0x01: "assembling", 
            0x02: "shelf",
            0x03: "screwing",
            0x04: "eol"
        }
    state_mapping = {
            0x00: "niO",
            0x01: "iO"
        }
    finished_mapping = {
            0x00: "not finished",
            0x01: "finished"
        }

    station_code = data.get("station")
    current_code = data.get("current")
    current = current_state_mapping.get(current_code, "unknown")
    station = stations_mapping.get(station_code, "unknown")

    finished_code = data.get("finished")
    finished = finished_mapping.get(finished_code, "unknown")

    cursor.execute("""
    UPDATE products
    SET  current = ?, timestamp = ?, finished = ?
    WHERE uid = ?
    """, (current, timestamp, finished, uid))


    # update the corresponding table 
    if station == "assembling":
        state = data["state"]    
        state = state_mapping.get(state, "unknown")
        cursor.execute("""
        UPDATE assembling
        SET state = ?, timestamp = ?
        WHERE uid = ?
        """, (state, timestamp, uid))

    elif station == "shelf":
        state = data["state"]
        state = state_mapping.get(state, "unknown")
        in_shelf = data["in_shelf"]
        position = data["position"]                
        cursor.execute("""
        UPDATE shelf
        SET state = ?, timestamp = ?, in_shelf = ?, position = ?
        WHERE uid = ?
        """, (state, timestamp, in_shelf, position, uid))

    elif station == "screwing":
        state = data["state"]
        state = state_mapping.get(state, "unknown")
        cursor.execute("""
        UPDATE screwing
        SET state = ?, timestamp = ?
        WHERE uid = ?
        """, (state, timestamp, uid))

    elif station == "eol":
        state = data["state"]
        state = state_mapping.get(state, "unknown")
        cursor.execute("""
        UPDATE end_of_line
        SET state = ?, timestamp = ?
        WHERE uid = ?
        """, (state, timestamp, uid))

    conn.commit()
    conn.close()


@app.route('/update', methods=['POST'])
def update():
    """
    Verarbeitet die Daten von einer Station und aktualisiert die Datenbank.
    """
    data = request.json  
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    if not data:
        return jsonify({"error": "didn't receive data"}), 400

    # update data for specific station
    update_station_data(data, timestamp)

    return jsonify({"message": f"Data successfully updated: ", "data": data})


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080)
