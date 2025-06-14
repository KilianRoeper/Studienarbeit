#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>

// Pin für SPI-Kommunikation
#define PN532_SS   D1

// Blockdefinitionen aus deinem Code
uint8_t UID_BLOCK = 10;
uint8_t ASSEMBLING_BLOCK = 11;
uint8_t SHELF_BLOCK = 12;
uint8_t SCREWING_BLOCK = 13;
uint8_t END_OF_LINE_BLOCK = 14;
uint8_t TRACKING_BLOCK = 15;

// built-in-uid params
uint8_t uid[] = {0};  
uint8_t uidLength;

// Initialisiere PN532
PN532_SPI intf(SPI, PN532_SS);
PN532 nfc = PN532(intf);

void setup() {
  Serial.begin(115200);
  nfc.begin();

  Serial.println("Initialisiere PN532...");
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 nicht gefunden!");
    while (1); // Stoppe hier
  }

  Serial.println("PN532 gefunden!");
  nfc.SAMConfig();
  Serial.println("Bereit, Blöcke zu leeren...");
}

void loop() {
  // Null-Daten definieren
  uint8_t emptyData[4] = {0x00, 0x00, 0x00, 0x00};

  // Liste der Blöcke
  uint8_t blocks[] = {UID_BLOCK, ASSEMBLING_BLOCK, SHELF_BLOCK, SCREWING_BLOCK, END_OF_LINE_BLOCK, TRACKING_BLOCK};

  Serial.println("waiting for a tag...");
  // Blöcke leeren
  while(!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)){}

  for (uint8_t i = 0; i < sizeof(blocks); i++) {
    uint8_t block = blocks[i];
    Serial.print("Leere Block "); Serial.println(block);

    bool success = nfc.mifareultralight_WritePage(block, emptyData);

    if (success) {
      Serial.print("Block "); Serial.print(block); Serial.println(" erfolgreich geleert.");
    } else {
      Serial.print("Fehler beim Leeren von Block "); Serial.println(block);
    }
  }

  // Warten, bevor erneut geleert wird
  Serial.println("Alle definierten Blöcke geleert.");
  delay(5000);
}
