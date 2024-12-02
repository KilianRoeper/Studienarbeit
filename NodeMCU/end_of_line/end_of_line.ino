#include <SPI.h>
#include <ArduinoJson.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <utils.h>
#include <ESP8266WiFi.h>


#define PN532_SS   D1

// blocks to read/ write at eol station
uint8_t UID_BLOCK = 10;
uint8_t END_OF_LINE_BLOCK = 14;
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

// end-of-line params
uint8_t uid_data[4] = {0};
uint8_t end_of_line_data[4] = {0};
uint8_t end_of_line_data_to_write[4] = {0};
uint8_t to_tracking_data[4] = {0, 0, 0, 0x01};
uint8_t at_tracking_data = 0x07;
uint8_t which_station = 0x04;

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
  Serial.println("eol station");

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
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  nfc.SAMConfig();
  Serial.println("Waiting for an ISO14443A Card ...");
}

void loop(void) {

  uint8_t success = false;

  //wait until a tag is found
  bool currentTagState = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if (currentTagState) {
    // tag in scope
    tag_detached_counter = 0;

    if(start_send_during_in_scope_state){
      start_send_during_in_scope_state = false;
      start_send_during_detached_state = true;

      if (uidLength == 7) {

        // processing end of line state
        random_state(end_of_line_data_to_write);
        success = nfc.mifareultralight_WritePage(END_OF_LINE_BLOCK, end_of_line_data_to_write);

        if (success) {

          memcpy(end_of_line_data, end_of_line_data_to_write, sizeof(end_of_line_data_to_write));
          tag_mem_print("end of line state", END_OF_LINE_BLOCK, end_of_line_data, nfc);
        }
        else {

          Serial.print("Unable to write to block ");Serial.println(END_OF_LINE_BLOCK);
        }

        // processing UID
        success = nfc.mifareultralight_ReadPage(UID_BLOCK, uid_data);

        if (success) {

          Serial.print("successfully read UID: ");
          nfc.PrintHexChar(uid_data, 4);
          Serial.println();
        }
        else {

          Serial.print("Unable to read/write page ");Serial.println(UID_BLOCK);
        }
        // processing tracking state
        success = nfc.mifareultralight_WritePage(TRACKING_BLOCK, to_tracking_data);

        if (success) {

          tag_mem_print("Tracking state", TRACKING_BLOCK, to_tracking_data, nfc);
        }
        else {

          Serial.print("Unable to write to page ");Serial.println(TRACKING_BLOCK);
        }
        // sending at eol
        send_data_to_central_pi(uid_data, &end_of_line_data[0], &at_tracking_data, &which_station);
      }
      else {

        Serial.println("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
      }
    }
  }
  else{
    // tag detached
    if(start_send_during_detached_state){
      tag_detached_counter += 1;
      if(tag_detached_counter > DEBOUNCE_DELAY){
        send_data_to_central_pi(uid_data, &end_of_line_data[0], &to_tracking_data[3], &which_station);
        tag_detached_counter = 0;
        start_send_during_in_scope_state = true;
        start_send_during_detached_state = false;
      }
    }
  }
}





