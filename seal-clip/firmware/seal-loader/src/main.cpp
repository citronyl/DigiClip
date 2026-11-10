#include <Arduino.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include "chunk.h"          // copied from chunks/chunk_N.h by load_all.sh

#define F_CS   PIN4_bm      // PA4
#define F_SCK  PIN3_bm      // PA3
#define F_MOSI PIN6_bm      // PA6
#define F_MISO PIN5_bm      // PA5

void csLow()  { PORTA.OUTCLR = F_CS; }
void csHigh() { PORTA.OUTSET = F_CS; }

uint8_t spi(uint8_t out) {
  uint8_t in = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (out & 0x80) PORTA.OUTSET = F_MOSI; else PORTA.OUTCLR = F_MOSI;
    out <<= 1;
    PORTA.OUTSET = F_SCK;
    in <<= 1;
    if (PORTA.IN & F_MISO) in |= 1;
    PORTA.OUTCLR = F_SCK;
  }
  return in;
}

void sendAddr(uint32_t a) { spi(a >> 16); spi(a >> 8); spi(a); }
void writeEnable()        { csLow(); spi(0x06); csHigh(); }
void waitReady()          { csLow(); spi(0x05); while (spi(0) & 0x01) {} csHigh(); }

void setup() {
  eeprom_update_byte((uint8_t*)3, 0x00);        // clear the "finished" marker

  PORTA.OUTSET = F_CS;
  PORTA.OUTCLR = F_SCK | F_MOSI;
  PORTA.DIRSET = F_CS | F_SCK | F_MOSI;
  PORTA.DIRCLR = F_MISO;

  csLow(); spi(0xAB); csHigh();                 // wake the flash if the player left it asleep
  _delay_us(10);

  uint8_t err = 0;
  csLow(); spi(0x9F);                           // JEDEC ID must be EF 70 18
  uint8_t m = spi(0), t = spi(0), c = spi(0);
  csHigh();
  if (m != 0xEF || t != 0x70 || c != 0x18) err = 1;

  if (!err) {                                   // erase this chunk's six 4 KB sectors
    for (uint8_t s = 0; s < 6; s++) {
      writeEnable();
      csLow(); spi(0x20); sendAddr(CHUNK_OFFSET + s * 4096UL); csHigh();
      waitReady();
    }
  }

  if (!err) {                                   // program 256-byte pages
    for (uint16_t p = 0; p < CHUNK_LEN; p += 256) {
      uint16_t end = (CHUNK_LEN - p > 256) ? p + 256 : CHUNK_LEN;
      writeEnable();
      csLow(); spi(0x02); sendAddr(CHUNK_OFFSET + p);
      for (uint16_t i = p; i < end; i++) spi(pgm_read_byte(&chunk[i]));
      csHigh();
      waitReady();
    }
  }

  if (!err) {                                   // read back and compare
    csLow(); spi(0x03); sendAddr(CHUNK_OFFSET);
    for (uint16_t i = 0; i < CHUNK_LEN; i++) {
      if (spi(0) != pgm_read_byte(&chunk[i])) { err = 2; break; }
    }
    csHigh();
  }

  eeprom_update_byte((uint8_t*)0, CHUNK_INDEX);
  eeprom_update_byte((uint8_t*)1, err ? 0xEE : 0xAA);
  eeprom_update_byte((uint8_t*)2, err);         // 1 = wrong ID, 2 = verify failed
  eeprom_update_byte((uint8_t*)3, 0x5A);        // finished
}

void loop() {}