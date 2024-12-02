#include <EEPROM.h>

#define EEPROM_UID_OFFSET_ADDR 0x03


uint32_t FIRST_UID = 0xABCDEF00;
uint32_t FIRST_OFFSET = 0x00;


void initalise_EEPROM_with_first_uid_and_offset(uint32_t first_uid = FIRST_UID, uint32_t first_offset = FIRST_OFFSET){
  // making sure that the first uid written to a tag is only going to be saved once in EEPROM
    uint32_t uid = 0;
    uint8_t offset = 0;
    
    for (int i = 5; i < 9; i++) {
      uid |= (EEPROM.read(i) << (8 * i));
    }
    for (int i = 4; i < 5; i++) {
      offset |= (EEPROM.read(i) << (8 * i));
    }
    
    if(uid == 0xABCDEF00){
      Serial.println("");
      Serial.print("uid already equal to ");
      Serial.print(uid, HEX);
      Serial.println(" in EEPROM");
    }
    else {
      for (int i = 5; i < 9; i++) {
        EEPROM.write(i, (first_uid >> (8 * i)) & 0xFF);
      }
    }
    if(offset == 0x00){
      Serial.print("offset already equal to ");
      Serial.print(offset, HEX);
      Serial.println(" in EEPROM");
    }
    else {
      EEPROM.put(EEPROM_UID_OFFSET_ADDR, offset);
    }
    EEPROM.commit();  
}


void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  initalise_EEPROM_with_first_uid_and_offset(FIRST_UID, FIRST_OFFSET);
}

void loop() {
  // put your main code here, to run repeatedly:

}
