#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>  // for arduino specific data types 
#include <ArduinoJson.h>


void prepare_data(JsonDocument& json_data, uint8_t uid[4], uint8_t *io_state, uint8_t *tracking_data);

void random_state(uint8_t rand_param[4]);

#endif  // UTILS_H
