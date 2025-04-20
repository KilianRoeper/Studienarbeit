import sqlite3

def fetch_all_data(db_path):
    """
    Ruft alle Daten aus der Datenbank ab und gibt sie lesbar aus.
    :param db_path: Pfad zur SQLite-Datenbankdatei
    """
    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()

        print("Daten aus der Tabelle 'products':")
        cursor.execute("SELECT * FROM products")
        uids_data = cursor.fetchall()
        for row in uids_data:
            print(row)
        print()

        print("Daten aus der Tabelle 'assembling':")
        cursor.execute("SELECT * FROM assembling")
        assembling_data = cursor.fetchall()
        for row in assembling_data:
            print(row)
        print()

        print("Daten aus der Tabelle 'shelf':")
        cursor.execute("SELECT * FROM shelf")
        shelf_data = cursor.fetchall()
        for row in shelf_data:
            print(row)
        print()

        print("Daten aus der Tabelle 'screwing':")
        cursor.execute("SELECT * FROM screwing")
        screwing_data = cursor.fetchall()
        for row in screwing_data:
            print(row)
        print()

        print("Daten aus der Tabelle 'end_of_line':")
        cursor.execute("SELECT * FROM end_of_line")
        end_of_line_data = cursor.fetchall()
        for row in end_of_line_data:
            print(row)
        print()

        conn.close()

    except sqlite3.Error as e:
        print(f"Fehler bei der Datenbankabfrage: {e}")
    except Exception as e:
        print(f"Allgemeiner Fehler: {e}")

if __name__ == "__main__":
    database_path = "rfid_data.db"  

    print("Daten aus der Datenbank werden abgefragt...")
    fetch_all_data(database_path)
