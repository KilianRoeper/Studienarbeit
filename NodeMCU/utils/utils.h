#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>  // for arduino specific data types 
#include <PN532.h>
#include <ESP8266WiFi.h>


void send_data_to_central_pi(uint8_t uid_data[4], uint8_t *station_data, uint8_t *tracking_data, uint8_t *which_station);

void random_state(uint8_t rand_param[4]);

void tag_mem_print(String what, uint8_t bock_num, uint8_t data[4], PN532 &nfc);

#endif  // UTILS_H
