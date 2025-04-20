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
    """
    insert new UIDs to all database tables 

    @param uid: uid of the nfc tag
    :type uid: bytestring
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
    
    @param data: Parsed JSON data to write to the database from stations
    @param timestamp: Current timestamp
    :type data: dict
    :type timestamp: str
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

    tracking_code = data.get("tracking")
    tracking = current_state_mapping.get(tracking_code, "unknown")

    finished_code = data.get("finished")
    finished = finished_mapping.get(finished_code, "unknown")

    cursor.execute("""
    UPDATE products
    SET  tracking = ?, timestamp = ?, finished = ?
    WHERE uid = ?
    """, (tracking, timestamp, finished, uid))


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
        position = data.get("position")            
        cursor.execute("""
        UPDATE shelf
        SET state = ?, timestamp = ?, position = ?
        WHERE uid = ?
        """, (state, timestamp, position, uid))

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
    
def print_payload(data, topic):

    uid_hex = data.get("UID")  
    
    uid = "".join(uid_hex)
    tracking = data.get("tracking", False)
    state = data.get("state", False)
    finished = data.get("finished", False)
    position = data.get("position", False)
    
    # mappings
    tracking_map = {
        1: "zum Regal",
        2: "beim Regal",
        3: "im Regal",
        4: "zum Schrauben",
        5: "beim Schrauben",
        6: "zu end of line",
        7: "bei end of line",
        8: "beim Zusammenbau",
        9: "Zum Zusammenbau"
    }
    
    tracking_text = tracking_map.get(tracking, f"Unbekanntes Tracking ({tracking})")

    print("\n")
    print("-" * 40)
    print(f"Topic:     {topic}")
    print(f"UID:       {uid}")
    print(f"Tracking:  {tracking_text}")
    print(f"Zustand:   {'in Ordnung' if state else 'nicht in Ordnung'}")
    print(f"Status:    {'Abgeschlossen' if finished else 'In Bearbeitung'}")    
    if(position):
        print(f"Regalfach: {position + 1}")
    print("-" * 40)

def on_connect(mqttc, userdata, flags, reason_code, properties):
    subscribe_topics(mqttc, userdata)
    print("connect...")
    print("reason_code: " + str(reason_code))


def on_message(mqttc, obj, msg):
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    
    # decode byte string and parse json content
    try:
        payload_str = msg.payload.decode("utf-8")  # from bytes to string
        data = json.loads(payload_str)             # json-string to python dict
    except (UnicodeDecodeError, json.JSONDecodeError) as e:
        print(f"Fehler beim Verarbeiten der Nachricht: {e}")
        return
    # update data for specific station
    update_station_data(data, timestamp, str(msg.topic))
    print_payload(data, str(msg.topic))


def on_subscribe(mqttc, obj, mid, reason_code_list, properties):
    print("Subscribed to topic: " + obj[mid - 1])

def subscribe_topics(client, topics):
    for topic in topics:
        client.subscribe(topic)
        
topics = [
    "rfid/assembling_mcu_send", 
    "rfid/shelf_mcu_send", 
    "rfid/screwing_mcu_send", 
    "rfid/eol_mcu_send"
    ]

mqttc = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, userdata=topics)
mqttc.on_message = on_message
mqttc.on_connect = on_connect
mqttc.on_subscribe = on_subscribe
mqttc.connect("10.42.0.1", 1883, 60)
mqttc.loop_forever()