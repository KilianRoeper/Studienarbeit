#include <SPI.h>
#include <ArduinoJson.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <utils.h>
#include <ESP8266WiFi.h>


#define PN532_SS   D1

// blocks to read/ write at screwing station
uint8_t UID_BLOCK = 10;
uint8_t SCREWING_BLOCK = 13;
uint8_t TRACKING_BLOCK = 15;

const char* ssid = "rfid_ritz_access_point";
const char* password = "rfid32_connect";
const char* serverIP2 = "10.42.0.1";
const int serverPort = 8080;

const uint8_t DEBOUNCE_DELAY = 3;
uint8_t tag_detached_counter = 0;
bool start_send_during_in_scope_state = true;
bool start_send_during_detached_state = false;

// built-in-uid params
uint8_t uid[] = {0};  
uint8_t uidLength;

// srewing params
uint8_t uid_data[4] = {0};
uint8_t screwing_data[4] = {0};
uint8_t screwing_data_to_write[4] = {0};
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

void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.println("screwing station");
  
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

  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN532 board");
    while (1); // halt
  }
  // Got ok data, print it out!
  Serial.print("Found chip PN532"); Serial.println((versiondata>>24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  nfc.SAMConfig();
  Serial.println("Waiting for an ISO14443A Card ...");
}

void loop() {
  
  uint8_t success = false;

  //wait until a tag is found
  bool currentTagState = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if(currentTagState) {
    // tag in scope
    tag_detached_counter = 0;

    if(start_send_during_in_scope_state){

      start_send_during_in_scope_state = false;
      start_send_during_detached_state = true;

      if (uidLength == 7) {

        // processing screwing state
        random_state(screwing_data_to_write);
        success = nfc.mifareultralight_WritePage(SCREWING_BLOCK, screwing_data_to_write);

        if (success) {

          memcpy(screwing_data, screwing_data_to_write, sizeof(screwing_data_to_write));
          tag_mem_print("Screwing state", SCREWING_BLOCK, screwing_data, nfc);
        }
        else {

          Serial.print("Unable to write to block ");Serial.println(SCREWING_BLOCK);
        }

        // processing UID
        success = nfc.mifareultralight_ReadPage(UID_BLOCK, uid_data);

        if (success) {

          Serial.println("successfully read UID: ");
          Serial.print("successfully read UID: ");
          nfc.PrintHexChar(uid_data, 4);
        }
        else {

          Serial.print("Unable to read/write page ");Serial.println(UID_BLOCK);
        }

        // processing tracking state -> to_eol
        success = nfc.mifareultralight_WritePage(TRACKING_BLOCK, to_tracking_data);
        
        if (success) {

          tag_mem_print("Tracking state", TRACKING_BLOCK, to_tracking_data, nfc);
        }
        else {

          Serial.print("Unable to write to page ");Serial.println(TRACKING_BLOCK);
        }
        // sending at_screwing
        send_data_to_central_pi(uid_data, &screwing_data[0], &at_tracking_data, &which_station);
      }
      else {

        Serial.println("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
      }
    }
  }
  else {
    // tag detached
    if(start_send_during_detached_state){
      tag_detached_counter += 1;
      if(tag_detached_counter > DEBOUNCE_DELAY){
        send_data_to_central_pi(uid_data, &screwing_data[0], &to_tracking_data[3], &which_station);
        tag_detached_counter = 0;
        start_send_during_in_scope_state = true;
        start_send_during_detached_state = false;
      }
    }
  }
}








