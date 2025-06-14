#include <SPI.h>
#include <ArduinoJson.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <utils.h>
#include <ESP8266WiFi.h>
#include <ArduinoMqttClient.h>


#define PN532_SS   D1
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
const char topic_send[] = "rfid/screwing_mcu_send";
const char topic_receive[] = "rfid/screwing_mcu_receive";

// blocks to read/ write at screwing station
uint8_t UID_BLOCK = 10;
uint8_t SCREWING_BLOCK = 13;
uint8_t TRACKING_BLOCK = 15;

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

// srewing params
uint8_t uid_data[UID_LENGTH] = {0};
uint8_t io_state[4] = {0};
uint8_t io_state_temp[4] = {0};
uint8_t to_tracking_data[4] = {0, 0, 0, 0x06};
uint8_t at_tracking_data = 0x05;
uint8_t which_station = 0x03;

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
  DEBUG_PRINTLN("screwing station");
  
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

  DEBUG_PRINT("Topic: ");
  DEBUG_PRINTLN(topic_receive);

  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    DEBUG_PRINT("Didn't find PN532 board");
    while (1); // halt
  }
  // Got ok data, print it out!
  DEBUG_PRINT("Found chip PN532"); DEBUG_PRINTLN_FMT((versiondata>>24) & 0xFF, HEX);
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

  //wait until a tag is found
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
    if(start_inscope | mqtt_message){
      start_inscope = false;
      mqtt_message = false;

      if (uidLength == 7) {
        // processing UID
        success = nfc.mifareultralight_ReadPage(UID_BLOCK, uid_data);
        if (success) {
          DEBUG_PRINTLN("successfully read UID: ");
          DEBUG_PRINT("successfully read UID: ");
          if (DEBUG){
            nfc.PrintHexChar(uid_data, 4);
          }
        }
        else {
          memset(uid_data, 0, sizeof(uid_data));
          DEBUG_PRINT("Unable to read/write page ");DEBUG_PRINTLN(UID_BLOCK);
        }
        
        // processing screwing state
        // random_state(screwing_data_to_write);
        success = nfc.mifareultralight_WritePage(SCREWING_BLOCK, io_state_temp);
        if (success) {
          memcpy(io_state, io_state_temp, sizeof(io_state_temp));
          print_block("Screwing state", SCREWING_BLOCK, io_state, nfc);
        }
        else {
          DEBUG_PRINT("Unable to write to block ");DEBUG_PRINTLN(SCREWING_BLOCK);
        }
        io_state_temp[3] = 0x00;

        // processing tracking state -> to_eol
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

        // sending at_screwing via mqtt
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
    // send via mqtt
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
