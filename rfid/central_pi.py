"""
module to maintain the database on received data
"""

import context  # Ensures paho is in PYTHONPATH
import sqlite3
import json
from datetime import datetime
import paho.mqtt.client as mqtt

DB_NAME = "rfid_data.db"

def initialize_uid(uid):
    """insert new UIDs to all database tables 

    @param uid (bytestring): uid of the nfc tag
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


def update_station_data(data, timestamp, topic):
    """update database with received data
    
    @param data (dict): Parsed JSON data to write to the database from stations
    @param timestamp (str): Current timestamp
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
    state_mapping = {
            0x00: "niO",
            0x01: "iO"
        }
    finished_mapping = {
            0x00: "not finished",
            0x01: "finished"
        }

    current_code = data.get("current")
    current = current_state_mapping.get(current_code, "unknown")

    finished_code = data.get("finished")
    finished = finished_mapping.get(finished_code, "unknown")

    cursor.execute("""
    UPDATE products
    SET  current = ?, timestamp = ?, finished = ?
    WHERE uid = ?
    """, (current, timestamp, finished, uid))


    # update the corresponding table 
    if topic == "rfid/assembling_mcu_send":
        state = data.get("state")    
        state = state_mapping.get(state, "unknown")
        cursor.execute("""
        UPDATE assembling
        SET state = ?, timestamp = ?
        WHERE uid = ?
        """, (state, timestamp, uid))

    elif topic == "rfid/shelf_mcu_send":
        state = data.get("state")
        state = state_mapping.get(state, "unknown")
        in_shelf = data.get("in_shelf")
        position = data.get("position")            
        cursor.execute("""
        UPDATE shelf
        SET state = ?, timestamp = ?, in_shelf = ?, position = ?
        WHERE uid = ?
        """, (state, timestamp, in_shelf, position, uid))

    elif topic == "rfid/screwing_mcu_send":
        state = data.get("state")
        state = state_mapping.get(state, "unknown")
        cursor.execute("""
        UPDATE screwing
        SET state = ?, timestamp = ?
        WHERE uid = ?
        """, (state, timestamp, uid))

    elif topic == "rfid/eol_mcu_send":
        state = data.get("state")
        state = state_mapping.get(state, "unknown")
        cursor.execute("""
        UPDATE end_of_line
        SET state = ?, timestamp = ?
        WHERE uid = ?
        """, (state, timestamp, uid))

    conn.commit()
    conn.close()

def on_connect(mqttc, obj, flags, reason_code, properties):
    print("reason_code: " + str(reason_code))


def on_message(mqttc, obj, msg):
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    
    # decode byte string and parse json content
    try:
        payload_str = msg.payload.decode("utf-8")  # from bytes to string
        data = json.loads(payload_str)            # json-string to python dict
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        print(f"Fehler beim Verarbeiten der Nachricht: {e}")
        return
    # update data for specific station
    update_station_data(data, timestamp, str(msg.topic))
    print(str(msg.topic) + " " + str(msg.qos) + " " + str(msg.payload))


def on_subscribe(mqttc, obj, mid, reason_code_list, properties):
    print("Subscribed: " + str(mid) + " " + str(reason_code_list))


def on_log(mqttc, obj, level, string):
    print(string)


# If you want to use a specific client id, use
# mqttc = mqtt.Client("client-id")
# but note that the client id must be unique on the broker. Leaving the client
# id parameter empty will generate a random id for you.
mqttc = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
mqttc.on_message = on_message
mqttc.on_connect = on_connect
mqttc.on_subscribe = on_subscribe
# Uncomment to enable debug messages
# mqttc.on_log = on_log
mqttc.connect("10.42.0.1", 1883, 60)
mqttc.subscribe("rfid/assembling_mcu_send")
mqttc.subscribe("rfid/shelf_mcu_send")
mqttc.subscribe("rfid/eol_mcu_send")
mqttc.subscribe("rfid/screwing_mcu_send")

mqttc.loop_forever()
