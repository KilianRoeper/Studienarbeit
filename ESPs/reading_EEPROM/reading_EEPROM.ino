#include <EEPROM.h>


void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  uint32_t offset = 0;
  uint32_t uid = 0;
  get_last_uid_offset_and_first_uid(&offset, &uid);
  Serial.println(" ");
  Serial.print("uid: ");
  Serial.println(uid, HEX);

  Serial.print("offset: ");
  Serial.println(offset, HEX);


}

void loop() {
  // put your main code here, to run repeatedly:

}

// getting UID offset from EEPROM
void get_last_uid_offset_and_first_uid(uint32_t *offset, uint32_t *first_uid) {
  for (int i = 0; i < 4; i++) {
    *offset |= (EEPROM.read(i) << (8 * i));
  }
  for (int i = 5; i < 9; i++) {
    *first_uid |= (EEPROM.read(i) << (8 * i));
  }
}
