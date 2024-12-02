#include <SPI.h>
#include <ArduinoJson.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <utils.h>
#include <ESP8266WiFi.h>

#define PN532_SS   D1
#define MAX_SHELF_POSITIONS 12
#define UID_LENGTH 4

// blocks to read/ write at shelf station
uint8_t UID_BLOCK = 10;
uint8_t ASSEMBLING_BLOCK = 11;
uint8_t SHELF_BLOCK = 12;
uint8_t SCREWING_BLOCK = 13;
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

// shelf params
uint8_t num_of_iO = 0;
uint8_t station_iO_niO[4] = {0};
uint8_t shelf_position[1] = {0};
uint8_t uid_data[4] = {0};
uint8_t shelf_data[4] = {0};
uint8_t shelf_data_to_write[4] = {0};
uint8_t to_tracking_data[4] = {0};
uint8_t at_tracking_data = 0x02;
uint8_t which_station = 0x02;

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

  // shelf params
  uint8_t success = false;

  bool currentTagState = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if(currentTagState) {
    // tag in scope
    tag_detached_counter = 0;

    if(start_send_during_in_scope_state){

      start_send_during_in_scope_state = false;
      start_send_during_detached_state = true;

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
        //determine_shelf_position(&shelf_position[0], uid_data);
        shelf_data[1] = 0x02;

        success = nfc.mifareultralight_WritePage(TRACKING_BLOCK, to_tracking_data);
        // writing tracking state
        if (success) {

          tag_mem_print("Tracking state", TRACKING_BLOCK, to_tracking_data, nfc);
        }
        else {

          Serial.print("Unable to write to page ");Serial.println(TRACKING_BLOCK);
        }

        // sending at_shelf
        send_data_to_central_pi(uid_data, &shelf_data[0], &at_tracking_data, &which_station);
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
        send_data_to_central_pi(uid_data, &shelf_data[0], &to_tracking_data[3], &which_station);
        tag_detached_counter = 0;
        start_send_during_in_scope_state = true;
        start_send_during_detached_state = false;
      }
    }
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

