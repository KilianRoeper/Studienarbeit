#include "utils.h"
#include <ArduinoJson.h>
#include <PN532.h>
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

void send_data_to_central_pi(uint8_t uid_data[4], uint8_t *station_data, uint8_t *tracking_data, uint8_t *which_station){
  String url = "http://" + String(serverIP) + ":8080/update";

  HTTPClient http;
  WiFiClient client;

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  // preparing data
  StaticJsonDocument<200> send_data;
  JsonArray uid_array = send_data.createNestedArray("UID");

  for (int i = 0; i < 4; i++) {
    char hexBuffer[3];
    sprintf(hexBuffer, "%02X", uid_data[i]);
    uid_array.add(hexBuffer);
  }
  send_data["current"] = tracking_data[0];  
  if (tracking_data[0] != 0x03) {
    send_data["finished"] = 0x00;
  }
  else {
    send_data["finished"] = 0x01;
  }
  
  send_data["station"] = which_station[0];

  // assembling
  if (*which_station == 1){
    send_data["state"] = station_data[3];                
  }
  // shelf
  if (*which_station == 2){
    send_data["state"] = station_data[3];   
    send_data["in_shelf"] = station_data[2]; 
    send_data["position"] = station_data[1];
  }
  // screwing
  if (*which_station == 3){
    send_data["state"] = station_data[3];                
  }
  // eol
  if (*which_station == 4){
    send_data["state"] = station_data[3];                
  }
  
  String jsonString;
  serializeJson(send_data, jsonString);

  // send to central PI
  int httpResponseCode = http.POST(jsonString);

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    String responseBody = http.getString();
    Serial.print("Response Body: ");
    Serial.println(responseBody);
  } else {
    Serial.print("Error on sending POST: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }

  http.end();

  //Serial.println(jsonString);
}

void tag_mem_print(String what, uint8_t bock_num, uint8_t data[4], PN532 &nfc){
  Serial.print(what);
  Serial.print(" at block ");
  Serial.print(bock_num);
  Serial.print(": ");
  nfc.PrintHexChar(data, 4);
  Serial.println("");
}