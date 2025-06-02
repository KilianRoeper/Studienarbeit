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
#define UID_LENGTH 4
#define DEBUG 1  // auf 0 setzen, um Debug auszuschalten

#if DEBUG
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINT_FMT(x, f) Serial.print(x, f)          // FMT - format
  #define DEBUG_PRINTLN_FMT(x, f) Serial.println(x, f)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINT_FMT(x, f)
  #define DEBUG_PRINTLN_FMT(x, f)
#endif

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

// global logic variables
const unsigned long INSCOPE_TIME = 2000;    // [ms]
bool start_inscope = true;
bool start_detached = false;
bool found_tag = false;
unsigned long inscope_start_time = 0;
bool mqtt_message = false;

// built-in-uid params
uint8_t uid[7] = {0};  
uint8_t uidLength;

// initialisation of assembling-station parameters
uint8_t uid_data[UID_LENGTH] = {0};
uint8_t io_state[4] = {0};
uint8_t io_state_temp[4] = {0};
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
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("assembling station");

  EEPROM.begin(512);

  DEBUG_PRINT("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED){
    delay(1000);
    DEBUG_PRINT (".");
  }
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("WiFi connected..!");
  DEBUG_PRINT("Got IP: ");  
  DEBUG_PRINTLN(WiFi.localIP());

	DEBUG_PRINT("Attempting to connect to the MQTT broker: ");
  DEBUG_PRINTLN(broker);

  if (!mqttClient.connect(broker, port)) {
    DEBUG_PRINT("MQTT connection failed! Error code = ");
    DEBUG_PRINTLN(mqttClient.connectError());

    while (1);
  }

  DEBUG_PRINTLN("You're connected to the MQTT broker!");
  DEBUG_PRINTLN();

  // set the message receive callback
  mqttClient.onMessage(onMqttMessage);

  DEBUG_PRINT("Subscribing to topic: ");
  DEBUG_PRINTLN(topic_receive);
  DEBUG_PRINTLN();

  // subscribe to a topic
  mqttClient.subscribe(topic_receive);
  
  // topics can be unsubscribed using:
  // mqttClient.unsubscribe(topic);

  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    DEBUG_PRINT("Didn't find PN532 board");
    while (1); // halt
  }
  // Got ok data, print it out!
  DEBUG_PRINTLN("");
  DEBUG_PRINT("Found chip PN5"); DEBUG_PRINTLN_FMT((versiondata>>24) & 0xFF, HEX);
  DEBUG_PRINT("Firmware ver. "); DEBUG_PRINT_FMT((versiondata>>16) & 0xFF, DEC);
  DEBUG_PRINT('.'); DEBUG_PRINTLN_FMT((versiondata>>8) & 0xFF, DEC);
  nfc.SAMConfig();
  DEBUG_PRINTLN("Waiting for an ISO14443A Card ...");
}


void loop(void) {
  // call poll() regularly to allow the library to receive MQTT messages and
  // send MQTT keep alive which avoids being disconnected from the broker
  mqttClient.poll();

  // set write-success variable
  uint8_t success = false;

  // wait until a tag is found
  bool currentTagState = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  if(currentTagState) {
    if(!start_detached){
      // handle in-scope timer
      if (!found_tag){
        found_tag = true;
        inscope_start_time = millis();
      }
      DEBUG_PRINT("tag found - time in scope [ms]: ");
      DEBUG_PRINTLN(millis() - inscope_start_time);
      if (millis() - inscope_start_time >= INSCOPE_TIME){
        DEBUG_PRINT("INSCOPE_TIME reached after [ms]: ");
        DEBUG_PRINTLN(millis() - inscope_start_time);
        DEBUG_PRINTLN("tag is kept at station");
        found_tag = false;
        start_detached = true;
      }
    }
    // handle functionality for a recognized tag
    if (start_inscope | mqtt_message){
      start_inscope = false;
      mqtt_message = false;

      if (uidLength == 7) {
        // processing UID
        success = nfc.mifareultralight_ReadPage(UID_BLOCK, uid_data);
        if (success) {
          modify_uid_if_zero(UID_BLOCK, uid_data, nfc);
        }
        else {
          DEBUG_PRINT("Unable to read/write page ");DEBUG_PRINTLN(UID_BLOCK);
        }
        
        // processing assembling state
        success = nfc.mifareultralight_WritePage(ASSEMBLING_BLOCK, io_state_temp);
        if (success) {
          memcpy(io_state, io_state_temp, sizeof(io_state_temp));
          print_block("Assembling state", ASSEMBLING_BLOCK, io_state, nfc);
        }
        else {
          DEBUG_PRINT("Unable to write to block ");DEBUG_PRINTLN(ASSEMBLING_BLOCK);
        }
        io_state_temp[3] = 0x00;

        // processing tracking state -> to_shelf
        success = nfc.mifareultralight_WritePage(TRACKING_BLOCK, to_tracking_data);
        if (success) {
          print_block("Tracking state", TRACKING_BLOCK, to_tracking_data, nfc);
        }
        else {
          DEBUG_PRINT("Unable to write to page ");DEBUG_PRINTLN(TRACKING_BLOCK);
        }

        // prepare and serialize the data
        StaticJsonDocument<256> json_data;
        String jsonString;
        prepare_data(json_data, uid_data, &io_state[0], &at_tracking_data);
        serializeJson(json_data, jsonString);

        // sending at_assembling via mqtt
        DEBUG_PRINTLN("send data to PI - in scope");
        mqttClient.beginMessage(topic_send);
        mqttClient.print(jsonString);
        mqttClient.endMessage();
      }
      else {
        DEBUG_PRINTLN("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
      }
    }
  }
  // tag detached
  else if(start_detached){
    // prepare and serialize the data
    StaticJsonDocument<256> json_data;
    String jsonString;
    prepare_data(json_data, uid_data, &io_state[0], &to_tracking_data[3]);
    serializeJson(json_data, jsonString);
    // send to shelf via mqtt
    DEBUG_PRINTLN("send data to PI - detached");
    mqttClient.beginMessage(topic_send);
    mqttClient.print(jsonString);
    mqttClient.endMessage();
    // alter global logic
    start_inscope = true;
    start_detached = false;
  }
  else {
    found_tag = false;
    start_inscope = true;
    DEBUG_PRINTLN("no tag found");
  }
}

void modify_uid_if_zero(uint8_t uid_block, uint8_t uid_param[4], PN532 &nfc){
  bool all_zero = true; 
  uint8_t success = false;

  for (int i = 0; i < 4; i++) {
    if (uid_param[i] != 0x00) { 
      all_zero = false;
      print_block("UID_not_zero", uid_block, uid_param, nfc);
      return; 
    }
  }
  if (all_zero) {

    get_last_uid_offset_and_first_uid(&uid_offset, &first_uid);
    DEBUG_PRINT("offset: ");
    DEBUG_PRINTLN_FMT(uid_offset, HEX);
    DEBUG_PRINT("first uid: ");
    DEBUG_PRINTLN_FMT(first_uid, HEX);
    determine_uid(uid_param, &uid_offset, &first_uid);

    uid_offset += 1;
    save_uid_offset(&uid_offset);
    DEBUG_PRINT("next offset: ");
    DEBUG_PRINTLN_FMT(uid_offset, HEX);

    success = nfc.mifareultralight_WritePage(uid_block, uid_param);
    if (success) {
      print_block("UID", uid_block, uid_param, nfc);
    }
    else {
      DEBUG_PRINT("Unable to write UID to page!");
      DEBUG_PRINTLN(uid_block);
    }
  } 
}

void determine_uid(uint8_t uid_param[4], uint8_t *uid_offset, uint32_t *first_uid) {
  uint32_t new_uid = *uid_offset + *first_uid;

  uid_param[0] = (new_uid >> 24) & 0xFF;
  uid_param[1] = (new_uid >> 16) & 0xFF;
  uid_param[2] = (new_uid >> 8) & 0xFF;
  uid_param[3] = new_uid & 0xFF;

  DEBUG_PRINT("new uid: ");
  DEBUG_PRINTLN_FMT(new_uid, HEX);
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
  DEBUG_PRINTLN("Received a message with topic '");
  DEBUG_PRINT(mqttClient.messageTopic());
  DEBUG_PRINT("', length ");
  DEBUG_PRINT(messageSize);
  DEBUG_PRINTLN(" bytes:");

  String receivedData = "";
  // use the Stream interface to print the contents
  while (mqttClient.available()) {
    receivedData += (char)mqttClient.read();
  }
  DEBUG_PRINT(receivedData);
  DEBUG_PRINTLN();
  DEBUG_PRINTLN();

  if(receivedData == "iO"){
    io_state_temp[0] = 0x00;
    io_state_temp[1] = 0x00;
    io_state_temp[2] = 0x00;
    io_state_temp[3] = 0x01;
  }
  else{
    io_state_temp[0] = 0x00;
    io_state_temp[1] = 0x00;
    io_state_temp[2] = 0x00;
    io_state_temp[3] = 0x00;
  }
  mqtt_message = true;
}

void print_block(String topic, uint8_t block_num, uint8_t data[4], PN532 &nfc){
  DEBUG_PRINT(topic);
  DEBUG_PRINT(" at block ");
  DEBUG_PRINT(block_num);
  DEBUG_PRINT(": ");
  if(DEBUG){
    nfc.PrintHexChar(data, 4);
  }
  DEBUG_PRINTLN("");
}
