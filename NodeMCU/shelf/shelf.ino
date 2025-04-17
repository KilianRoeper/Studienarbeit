#include <SPI.h>
#include <ArduinoJson.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <utils.h>
#include <ESP8266WiFi.h>
#include <ArduinoMqttClient.h>

#define PN532_SS   D1
#define MAX_SHELF_POSITIONS 12
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
const char topic_send[] = "rfid/shelf_mcu_send";
const char topic_receive[] = "rfid/shelf_mcu_receive";

// blocks to read/ write at shelf station
uint8_t UID_BLOCK = 10;
uint8_t ASSEMBLING_BLOCK = 11;
uint8_t SHELF_BLOCK = 12;
uint8_t SCREWING_BLOCK = 13;
uint8_t END_OF_LINE_BLOCK = 14;
uint8_t TRACKING_BLOCK = 15;

// global logic variables
const unsigned long INSCOPE_TIME = 2000;    // [ms]
bool start_send_inscope = true;
bool start_send_detached = false;
bool found_tag = false;
unsigned long detached_time = 0;
unsigned long inscope_time = 0;
bool mqtt_message = false;

// built-in-uid params
uint8_t uid[7] = {0};  
uint8_t uidLength;

// initialisation of shelf-station params
uint8_t io_cnt = 0;
uint8_t station_io[4] = {0};
uint8_t shelf_position[1] = {0};
uint8_t uid_data[UID_LENGTH] = {0};
uint8_t shelf_data[4] = {0};
uint8_t shelf_data_temp[4] = {0};
uint8_t to_tracking_data[4] = {0};
uint8_t at_tracking_data = 0x02;
uint8_t which_station = 0x02;

// structure to handle shelf postions 
typedef struct {
    uint8_t uid[UID_LENGTH];
    uint8_t shelf_position;
} UIDEntry;
UIDEntry shelf_map[MAX_SHELF_POSITIONS] = {0};

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

void setup() {
  Serial.begin(115200);
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("shelf station");

  DEBUG_PRINT("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED){
    delay(1000);
    DEBUG_PRINT(".");
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
  DEBUG_PRINT("Found chip PN5"); DEBUG_PRINTLN_FMT((versiondata>>24) & 0xFF, HEX);
  DEBUG_PRINT("Firmware ver. "); DEBUG_PRINT_FMT((versiondata>>16) & 0xFF, DEC);
  DEBUG_PRINT('.'); DEBUG_PRINTLN_FMT((versiondata>>8) & 0xFF, DEC);
  nfc.SAMConfig();
  DEBUG_PRINTLN("Waiting for an ISO14443A Card ...");
}

void loop() {
  // call poll() regularly to allow the library to receive MQTT messages and
  // send MQTT keep alive which avoids being disconnected from the broker
  mqttClient.poll();

  // set write-success variable
  uint8_t success = false;

  // wait until a tag is found
  bool currentTagState = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  if(currentTagState) {
    if(!start_send_detached){
      // handle in-scope timer
      if (!found_tag){
        found_tag = true;
        inscope_time = millis();
      }
      DEBUG_PRINT("tag found - time in scope [ms]: ");
      DEBUG_PRINTLN(millis() - inscope_time);
      if (millis() - inscope_time >= INSCOPE_TIME){
        DEBUG_PRINT("INSCOPE_TIME reached after [ms]: ");
        DEBUG_PRINTLN(millis() - inscope_time);
        DEBUG_PRINTLN("tag is kept at station");
        found_tag = false;
        start_send_detached = true;
      }
    }
    // handle functionality for a recognized tag
    if (start_send_inscope | mqtt_message){
      start_send_inscope = false;
      mqtt_message = false;

      if (uidLength == 7) {

        // processing shelf state
        // random_state(shelf_data_temp);
        success = nfc.mifareultralight_WritePage(SHELF_BLOCK, shelf_data_temp);
        if (success) {
          memcpy(shelf_data, shelf_data_temp, sizeof(shelf_data_temp));
          tag_mem_print("shelf state", SHELF_BLOCK, shelf_data, nfc);
        }
        else {
          DEBUG_PRINT("Unable to write to block ");DEBUG_PRINTLN(SHELF_BLOCK);
        }

        // processing UID
        success = nfc.mifareultralight_ReadPage(UID_BLOCK, uid_data);
        if (success) {
          DEBUG_PRINTLN("successfully read UID");
          if(DEBUG){
            nfc.PrintHexChar(uid_data, 4);
          }
          DEBUG_PRINTLN();
        }
        else {
          DEBUG_PRINT("Unable to read/write page ");DEBUG_PRINTLN(UID_BLOCK);
        }

        // processing tracking state
        nfc.mifareultralight_ReadPage(ASSEMBLING_BLOCK, station_io);
        io_cnt += station_io[3];
        nfc.mifareultralight_ReadPage(SHELF_BLOCK, station_io);
        io_cnt += station_io[3];
        nfc.mifareultralight_ReadPage(SCREWING_BLOCK, station_io);
        io_cnt += station_io[3];
        nfc.mifareultralight_ReadPage(END_OF_LINE_BLOCK, station_io);
        io_cnt += station_io[3];

        if (io_cnt >= 2) {
          // sending tag in_shelf
          to_tracking_data[3] = 0x03;
          // is in shelf
          shelf_data[2] = 0x01;
          // shelf position byte
          determine_shelf_position(&shelf_position[0], uid_data);
          shelf_data[1] = shelf_position[0];
        }
        else {
          // sending tag to screwing
          to_tracking_data[3] = 0x04;
          // not in_shelf
          shelf_data[2] = 0x00;
          // no shelf position
          shelf_data[1] = 0x00;
        }
        success = nfc.mifareultralight_WritePage(TRACKING_BLOCK, to_tracking_data);
        // writing tracking state
        if (success) {
          tag_mem_print("Tracking state", TRACKING_BLOCK, to_tracking_data, nfc);
        }
        else {
          DEBUG_PRINT("Unable to write to page ");DEBUG_PRINTLN(TRACKING_BLOCK);
        }
        // prepare and serialize the data
        StaticJsonDocument<256> json_data;
        String jsonString;
        prepare_data(json_data, uid_data, &shelf_data[0], &at_tracking_data);
        json_data["in_shelf"] = shelf_data[2]; 
        json_data["position"] = shelf_data[1];
        serializeJson(json_data, jsonString);

        // sending at_shelf via mqtt
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
  else if(start_send_detached){
    // prepare and serialize the data
    StaticJsonDocument<256> json_data;
    String jsonString;
    prepare_data(json_data, uid_data, &shelf_data[0], &to_tracking_data[3]);
    json_data["in_shelf"] = shelf_data[2]; 
    json_data["position"] = shelf_data[1];
    serializeJson(json_data, jsonString);
    // send via mqtt
    DEBUG_PRINTLN("send data to PI - detached");
    mqttClient.beginMessage(topic_send);
    mqttClient.print(jsonString);
    mqttClient.endMessage();
    // alter global logic 
    start_send_inscope = true;
    start_send_detached = false;
  }
  else {
    found_tag = false;
    start_send_inscope = true;
    DEBUG_PRINTLN("no tag found");
  }
}



bool compare_uid(uint8_t uid1[UID_LENGTH], uint8_t uid2[UID_LENGTH]) {
    return memcmp(uid1, uid2, UID_LENGTH) == 0;
}

int find_next_free_position() {
    for (int i = 0; i < MAX_SHELF_POSITIONS; i++) {
        if (shelf_map[i].shelf_position == 0) {
            return i + 1; 
        }
    }
    return -1;
}

void determine_shelf_position(uint8_t *shelf_position, uint8_t uid_data[4]) {
    // verify if uid is already being tracked 
    for (int i = 0; i < MAX_SHELF_POSITIONS; i++) {

        // products with know uid already in shelf
        if (shelf_map[i].shelf_position != 0 && compare_uid(shelf_map[i].uid, uid_data)) {
            *shelf_position = shelf_map[i].shelf_position;
            return;
        }
    }

    // find next free position to place product at 
    int new_position = find_next_free_position();

    // return if there is no free position
    if (new_position == -1) {
        *shelf_position = 0; 
        return;
    }

    // save uid and position in shelf_map struct array
    for (int i = 0; i < MAX_SHELF_POSITIONS; i++) {
        if (shelf_map[i].shelf_position == 0) {
            memcpy(shelf_map[i].uid, uid_data, UID_LENGTH);
            shelf_map[i].shelf_position = new_position;
            *shelf_position = new_position;
            return;
        }
    }
}

void onMqttMessage(int messageSize) {
  // received a message, print out the topic and contents
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
    shelf_data_temp[0] = 0x00;
    shelf_data_temp[1] = 0x00;
    shelf_data_temp[2] = 0x00;
    shelf_data_temp[3] = 0x01;
  }
  else{
    shelf_data_temp[0] = 0x00;
    shelf_data_temp[1] = 0x00;
    shelf_data_temp[2] = 0x00;
    shelf_data_temp[3] = 0x00;
  }
  mqtt_message = true;
}

void tag_mem_print(String what, uint8_t bock_num, uint8_t data[4], PN532 &nfc){
  DEBUG_PRINT(what);
  DEBUG_PRINT(" at block ");
  DEBUG_PRINT(bock_num);
  DEBUG_PRINT(": ");
  if(DEBUG){
    nfc.PrintHexChar(data, 4);
  }
  DEBUG_PRINTLN("");
}
