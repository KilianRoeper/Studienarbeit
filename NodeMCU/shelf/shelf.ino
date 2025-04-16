#include <SPI.h>
#include <ArduinoJson.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <utils.h>
#include <ESP8266WiFi.h>
#include <ArduinoMqttClient.h>
#include <Ticker.h>

#define PN532_SS   D1
#define MAX_SHELF_POSITIONS 12
#define UID_LENGTH 4

// wlan connection parameters
const char* ssid = "rfid_ritz_access_point";
const char* password = "rfid32_connect";

// mqtt connection parameters
const char broker[] = "10.42.0.1";
int port = 1883;
const char topic_send[] = "rfid/shelf_mcu_send";
const char topic_receive[] = "rfid/shelf_mcu_receive";
bool received_message_from_broker = false;

// blocks to read/ write at shelf station
uint8_t UID_BLOCK = 10;
uint8_t ASSEMBLING_BLOCK = 11;
uint8_t SHELF_BLOCK = 12;
uint8_t SCREWING_BLOCK = 13;
uint8_t END_OF_LINE_BLOCK = 14;
uint8_t TRACKING_BLOCK = 15;

// global logic varaibles
const uint8_t DEBOUNCE_DELAY = 200;
const uint8_t INSCOPE_TIME = 100;
bool start_send_inscope = true;
bool start_send_inscope_last = true;
bool start_send_detached = false;
bool start_send_detached_last = false;

// built-in-uid params
uint8_t uid[7] = {0};  
uint8_t uidLength;

// initialisation of shelf-station parameters
uint8_t num_of_iO = 0;
uint8_t station_iO_niO[4] = {0};
uint8_t shelf_position[1] = {0};
uint8_t uid_data[4] = {0};
uint8_t shelf_data[4] = {0};
uint8_t shelf_data_to_write[4] = {0};
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

// register tickers to handle counters via an ESP8266 interrupt (only has one core, so threading not possible)
Ticker detachedTicker;
Ticker inScopeTicker;
volatile int detached_cnt = 0;
volatile int inscope_cnt = 0;

// ticker callback to increment detached counter
void incr_detached(){
  detached_cnt++;
}

// ticker callback to increment in_scope counter
void incr_inscope(){
  inscope_cnt++;
}

void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.println("shelf station");

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
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  nfc.SAMConfig();
  Serial.println("Waiting for an ISO14443A Card ...");
}

void loop() {
  // call poll() regularly to allow the library to receive MQTT messages and
  // send MQTT keep alive which avoids being disconnected by the broker
  mqttClient.poll();

  // set write-success variable
  uint8_t success = false;

  // wait until a tag is found
  bool currentTagState = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  if(currentTagState) {
    // reset detached counter if a tag was found
    detached_cnt = 0;
    // handle the in-scope counter
    if (!inScopeTicker.active()){
      inScopeTicker.attach(0.01, incr_inscope);
    }
    if (inscope_cnt >= INSCOPE_TIME){
      start_send_detached = true;
    }
    // handle functionality for a recognized tag
    if (start_send_inscope){
      start_send_inscope = false;
      start_send_detached = true;

      if (uidLength == 7) {
        // processing shelf state
        random_state(shelf_data_to_write);
        success = nfc.mifareultralight_WritePage(SHELF_BLOCK, shelf_data_to_write);

        if (success) {
          memcpy(shelf_data, shelf_data_to_write, sizeof(shelf_data_to_write));
          tag_mem_print("shelf state", SHELF_BLOCK, shelf_data, nfc);
        }
        else {
          Serial.print("Unable to write to block ");Serial.println(SHELF_BLOCK);
        }
        // processing UID
        success = nfc.mifareultralight_ReadPage(UID_BLOCK, uid_data);
        if (success) {
          Serial.println("successfully read UID");
          nfc.PrintHexChar(uid_data, 4);
          Serial.println();
        }
        else {
          Serial.print("Unable to read/write page ");Serial.println(UID_BLOCK);
        }
        // processing tracking state
        nfc.mifareultralight_ReadPage(ASSEMBLING_BLOCK, station_iO_niO);
        num_of_iO += station_iO_niO[3];
        nfc.mifareultralight_ReadPage(SHELF_BLOCK, station_iO_niO);
        num_of_iO += station_iO_niO[3];
        nfc.mifareultralight_ReadPage(SCREWING_BLOCK, station_iO_niO);
        num_of_iO += station_iO_niO[3];
        nfc.mifareultralight_ReadPage(END_OF_LINE_BLOCK, station_iO_niO);
        num_of_iO += station_iO_niO[3];

        if (num_of_iO >= 2) {
          // sending in_shelf
          to_tracking_data[3] = 0x03;
          // true for "is in shelf"
          shelf_data[2] = 0x01;
          // shelf position byte
          determine_shelf_position(&shelf_position[0], uid_data);
          shelf_data[1] = shelf_position[0];
        }
        else {
          // sending to screwing
          to_tracking_data[3] = 0x04;
          // not in_shelf
          shelf_data[2] = 0x00;
          // no shelf position
          shelf_data[1] = 0x00;
        }
        to_tracking_data[3] = 0x03;
        // true for "is in shelf"
        shelf_data[2] = 0x01;
        // shelf position byte
        // determine_shelf_position(&shelf_position[0], uid_data);
        shelf_data[1] = 0x02;

        success = nfc.mifareultralight_WritePage(TRACKING_BLOCK, to_tracking_data);
        // writing tracking state
        if (success) {
          tag_mem_print("Tracking state", TRACKING_BLOCK, to_tracking_data, nfc);
        }
        else {
          Serial.print("Unable to write to page ");Serial.println(TRACKING_BLOCK);
        }
        // prepare and serialize the data
        StaticJsonDocument<256> json_data;
        String jsonString;
        prepare_data(json_data, uid_data, &shelf_data[0], &at_tracking_data);
        json_data["in_shelf"] = shelf_data[2]; 
        json_data["position"] = shelf_data[1];
        serializeJson(json_data, jsonString);

        // sending at_shelf via mqtt
        Serial.println("starting to send via mqtt - at shelf");
        mqttClient.beginMessage(topic_send);
        mqttClient.print(jsonString);
        mqttClient.endMessage();
        Serial.println("finished sending via mqtt");
      }
      else {
        Serial.println("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
      }
    }
  }
  // tag detached
  else if(start_send_detached){
    inscope_cnt = 0;
    if(!detachedTicker.active()){
      detachedTicker.attach(0.01, incr_detached);
    }
    if(inScopeTicker.active()){
      inScopeTicker.detach();
    }
    if(detached_cnt > DEBOUNCE_DELAY){
      // prepare and serialize the data
      StaticJsonDocument<256> json_data;
      String jsonString;
      prepare_data(json_data, uid_data, &shelf_data[0], &to_tracking_data[3]);
      json_data["in_shelf"] = shelf_data[2]; 
      json_data["position"] = shelf_data[1];
      serializeJson(json_data, jsonString);
      // send via mqtt
      Serial.println("starting to send via mqtt - dispatched from shelf");
      mqttClient.beginMessage(topic_send);
      mqttClient.print(jsonString);
      mqttClient.endMessage();
      Serial.println("finished sending via mqtt");
      // alter global logic 
      detached_cnt = 0;
      start_send_inscope = true;
      start_send_detached = false;
      if (detachedTicker.active()){
        detachedTicker.detach();
      }
    }
  }
  else {
    inscope_cnt = 0;
    detached_cnt = 0;
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
    shelf_data_to_write[0] = 0x00;
    shelf_data_to_write[1] = 0x00;
    shelf_data_to_write[2] = 0x00;
    shelf_data_to_write[3] = 0x01;
  }
  else{
    shelf_data_to_write[0] = 0x00;
    shelf_data_to_write[1] = 0x00;
    shelf_data_to_write[2] = 0x00;
    shelf_data_to_write[3] = 0x00;
  }
  received_message_from_broker = true;
}