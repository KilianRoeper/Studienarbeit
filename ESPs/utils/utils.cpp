#include "utils.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>



void random_state(uint8_t rand_param[4]){
  if (random(2) == 1) {
    rand_param[3] = 0x01;
  }
  else {
    rand_param[3] = 0x00;
  }
}

const char* serverIP = "10.42.0.1";

void prepare_data(JsonDocument& json_data, uint8_t uid[4], uint8_t *io_state, uint8_t *tracking_data){

  // preparing data
  JsonArray uid_array = json_data.createNestedArray("UID");

  for (int i = 0; i < 4; i++) {
    char hexBuffer[3];
    sprintf(hexBuffer, "%02X", uid[i]);
    uid_array.add(hexBuffer);
  }
  json_data["tracking"] = tracking_data[0];  
  if (tracking_data[0] != 0x03) {
    json_data["finished"] = 0x00;
  }
  else {
    json_data["finished"] = 0x01;
  }
  json_data["state"] = io_state[3];  
}