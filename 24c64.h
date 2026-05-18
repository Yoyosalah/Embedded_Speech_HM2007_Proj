#ifndef EEPROM_24C64_H_
#define EEPROM_24C64_H_

#include <stdint.h>

// 24C64 has 8KB capacity (8192 bytes)
#define EEPROM_SIZE 8192
#define EEPROM_PAGE_SIZE 32  // 32 bytes per page
#define EEPROM_ADDRESS 0x50  // Default I2C address (A0=A1=A2=0)

// Function prototypes
void eeprom_write_byte(uint16_t addr, uint8_t data);
uint8_t eeprom_read_byte(uint16_t addr);
void eeprom_write_page(uint16_t addr, uint8_t* data, uint8_t len);
void eeprom_read_sequential(uint16_t addr, uint8_t* buffer, uint16_t len);

#endif
