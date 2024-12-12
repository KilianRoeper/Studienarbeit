import context  # Ensures paho is in PYTHONPATH
import paho.mqtt.publish as publish

publish.single("rfid/assembling_mcu_send", "iO", hostname="10.42.0.1")
publish.single("rfid/shelf_mcu_send", "iO", hostname="10.42.0.1")
publish.single("rfid/screwing_mcu_send", "niO", hostname="10.42.0.1")
publish.single("rfid/eol_mcu_send", "niO", hostname="10.42.0.1")