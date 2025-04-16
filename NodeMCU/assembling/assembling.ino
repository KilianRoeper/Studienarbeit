#include <SPI.h>
#include <ArduinoJson.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <EEPROM.h>
#include <utils.h>
#include <ESP8266WiFi.h>
#include <ArduinoMqttClient.h>

#define PN532_SS   D1
#define EEPROM_UID_OFFSET_ADDR 0x03

// wlan connection parameters
const char* ssid = "rfid_ritz_access_point";
const char* password = "rfid32_connect";

// mqtt connection parameters
const char broker[] = "10.42.0.1";
int port = 1883;
const char topic_send[] = "rfid/assembling_mcu_send";
const char topic_receive[] = "rfid/assembling_mcu_receive";
bool received_message_from_broker = false;

// blocks to read/ write at assembling station
uint8_t UID_BLOCK = 10;
uint8_t ASSEMBLING_BLOCK = 11;
uint8_t TRACKING_BLOCK = 15;

// uid initialization parameters
uint32_t FIRST_UID = 0xABCDEF00;
uint32_t FIRST_OFFSET = 0x00000000;
uint8_t uid_offset = FIRST_OFFSET;
uint32_t first_uid = FIRST_UID;

// global logic varaibles
const uint8_t DEBOUNCE_DELAY = 3;
uint8_t tag_detached_counter = 0;
bool start_send_during_in_scope_state = true;
bool start_send_during_detached_state = false;

// built-in-uid params
uint8_t uid[7] = {0};  
uint8_t uidLength;

// initialisation of assembling-station parameters
uint8_t uid_data[4] = {0};
uint8_t assembling_data[4] = {0};
uint8_t assembling_data_to_write[4] = {0};
uint8_t to_tracking_data[4] = {0, 0, 0, 0x01};
uint8_t at_tracking_data = 0x08;

// SPI-connection
PN532_SPI intf(SPI, PN532_SS);
PN532 nfc = PN532(intf);

// I2C connection
//#include <Wire.h>
//#include <PN532_I2C.h>
//PN532_I2C intf(Wire);
//PN532 nfc = PN532(intf);

// wifi and mqtt client 
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

void setup(void) {
  Serial.begin(115200);
  Serial.println("");
  Serial.println("assembling station");

  EEPROM.begin(512);

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED){
    delay(1000);
    Serial.print (".");
  }
  Serial.println("");
  Serial.println("WiFi connected..!");
  Serial.print("Got IP: ");  
  Serial.println(WiFi.localIP());

	Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(broker);

  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  Serial.println("You're connected to the MQTT broker!");
  Serial.println();

  // set the message receive callback
  mqttClient.onMessage(onMqttMessage);

  Serial.print("Subscribing to topic: ");
  Serial.println(topic_receive);
  Serial.println();

  // subscribe to a topic
  mqttClient.subscribe(topic_receive);
  
  // topics can be unsubscribed using:
  // mqttClient.unsubscribe(topic);

  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN532 board");
    while (1); // halt
  }
  // Got ok data, print it out!
  Serial.println("");
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  nfc.SAMConfig();
  Serial.println("Waiting for an ISO14443A Card ...");
}


void loop(void) {

  // call poll() regularly to allow the library to receive MQTT messages and
  // send MQTT keep alive which avoids being disconnected by the broker
  mqttClient.poll();

  // set write-success variable
  uint8_t success = false;

  // wait until a tag is found
  bool currentTagState = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if(currentTagState == true && received_message_from_broker == true) {
    // tag in scope
    tag_detached_counter = 0;

    if(start_send_during_in_scope_state == true){
      start_send_during_in_scope_state = false;
      start_send_during_detached_state = true;

      if (uidLength == 7) {
        // processing assembling state
        success = nfc.mifareultralight_WritePage(ASSEMBLING_BLOCK, assembling_data_to_write);

        if (success) {
          memcpy(assembling_data, assembling_data_to_write, sizeof(assembling_data_to_write));
          tag_mem_print("Assembling state", ASSEMBLING_BLOCK, assembling_data, nfc);
        }
        else {
          Serial.print("Unable to write to block ");Serial.println(ASSEMBLING_BLOCK);
        }

        // processing UID
        success = nfc.mifareultralight_ReadPage(UID_BLOCK, uid_data);

        if (success) {
          modify_uid_if_zero(UID_BLOCK, uid_data, nfc);
        }
        else {
          Serial.print("Unable to read/write page ");Serial.println(UID_BLOCK);
        }

        // processing tracking state -> to_shelf
        success = nfc.mifareultralight_WritePage(TRACKING_BLOCK, to_tracking_data);

        if (success) {
          tag_mem_print("Tracking state", TRACKING_BLOCK, to_tracking_data, nfc);
        }
        else {
          Serial.print("Unable to write to page ");Serial.println(TRACKING_BLOCK);
        }

        // prepare and serialize the data
        StaticJsonDocument<256> json_data;
        String jsonString;
        prepare_data(json_data, uid_data, &assembling_data[0], &at_tracking_data);
        serializeJson(json_data, jsonString);

        // sending at_assembling via mqtt
        mqttClient.beginMessage(topic_send);
        mqttClient.print(jsonString);
        mqttClient.endMessage();
      }
      else {
        Serial.println("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
      }
    }
  }
  // tag detached
  else if(start_send_during_detached_state == true){
    tag_detached_counter += 1;
    if(tag_detached_counter >= DEBOUNCE_DELAY){
      // prepare and serialize the data
      StaticJsonDocument<256> json_data;
      String jsonString;
      prepare_data(json_data, uid_data, &assembling_data[0], &to_tracking_data[3]);
      serializeJson(json_data, jsonString);
      // send to shelf
      mqttClient.beginMessage(topic_send);
      mqttClient.print(jsonString);
      mqttClient.endMessage();
      // alter global logic
      tag_detached_counter = 0;
      start_send_during_in_scope_state = true;
      start_send_during_detached_state = false;
      received_message_from_broker = false;
    }
  }
}

void modify_uid_if_zero(uint8_t uid_block, uint8_t uid_param[4], PN532 &nfc){
  bool all_zero = true; 
  uint8_t success = false;

  for (int i = 0; i < 4; i++) {
    if (uid_param[i] != 0x00) { 
      all_zero = false;
      tag_mem_print("UID_not_zero", uid_block, uid_param, nfc);
      return; 
    }
  }
  if (all_zero) {

    get_last_uid_offset_and_first_uid(&uid_offset, &first_uid);
    Serial.print("offset: ");
    Serial.println(uid_offset, HEX);
    Serial.print("first uid: ");
    Serial.println(first_uid, HEX);
    determine_uid(uid_param, &uid_offset, &first_uid);

    uid_offset += 1;
    save_uid_offset(&uid_offset);
    Serial.print("next offset: ");
    Serial.println(uid_offset, HEX);

    success = nfc.mifareultralight_WritePage(uid_block, uid_param);
    if (success) {
      tag_mem_print("UID", uid_block, uid_param, nfc);
    }
    else {
      Serial.print("Unable to write UID to page!");
      Serial.println(uid_block);
    }
  } 
}

void determine_uid(uint8_t uid_param[4], uint8_t *uid_offset, uint32_t *first_uid) {
  uint32_t new_uid = *uid_offset + *first_uid;

  uid_param[0] = (new_uid >> 24) & 0xFF;
  uid_param[1] = (new_uid >> 16) & 0xFF;
  uid_param[2] = (new_uid >> 8) & 0xFF;
  uid_param[3] = new_uid & 0xFF;

  Serial.print("new uid: ");
  Serial.println(new_uid, HEX);
}

// getting UID offset from EEPROM
void get_last_uid_offset_and_first_uid(uint8_t *offset, uint32_t *first_uid) {
  EEPROM.get(EEPROM_UID_OFFSET_ADDR, *offset);
  for (int i = 5; i < 9; i++) {
    *first_uid |= (EEPROM.read(i) << (8 * i));
  }
}

// saving UID offset to EEPROM
void save_uid_offset(uint8_t *offset) {
  EEPROM.put(EEPROM_UID_OFFSET_ADDR, *offset);
  EEPROM.commit();  // Ensure data is saved to EEPROM
}

void onMqttMessage(int messageSize) {
  // we received a message, print out the topic and contents
  Serial.println("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.println(" bytes:");

  String receivedData = "";
  // use the Stream interface to print the contents
  while (mqttClient.available()) {
    receivedData += (char)mqttClient.read();
  }
  Serial.print(receivedData);
  Serial.println();
  Serial.println();

  if(receivedData == "iO"){
    assembling_data_to_write[0] = 0x00;
    assembling_data_to_write[1] = 0x00;
    assembling_data_to_write[2] = 0x00;
    assembling_data_to_write[3] = 0x01;
  }
  else{
    assembling_data_to_write[0] = 0x00;
    assembling_data_to_write[1] = 0x00;
    assembling_data_to_write[2] = 0x00;
    assembling_data_to_write[3] = 0x00;
  }
  received_message_from_broker = true;
}