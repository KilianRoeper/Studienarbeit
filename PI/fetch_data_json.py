import sqlite3
import json

def fetch_all_data_json(db_path):
    """
    Ruft alle Daten aus der Datenbank ab und gibt sie im JSON-Format aus.
    :param db_path: Pfad zur SQLite-Datenbankdatei
    """
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()

        # Tabellen in ein Dictionary laden
        data = {}

        for table in ["products", "assembling", "shelf", "screwing", "end_of_line"]:
            cursor.execute(f"SELECT * FROM {table}")
            columns = [desc[0] for desc in cursor.description]  # Spaltennamen
            rows = cursor.fetchall()
            data[table] = [dict(zip(columns, row)) for row in rows]

        conn.close()

        # Daten im JSON-Format ausgeben
        print(json.dumps(data, indent=4))

    except sqlite3.Error as e:
        print(f"Fehler bei der Datenbankabfrage: {e}")
    except Exception as e:
        print(f"Allgemeiner Fehler: {e}")

if __name__ == "__main__":
    database_path = "rfid_data.db"
    print("Daten aus der Datenbank werden abgefragt und im JSON-Format angezeigt...")
    fetch_all_data_json(database_path)
