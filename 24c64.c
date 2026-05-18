#include "24c64.h"
#include "i2c.h"
#include <util/delay.h>

void eeprom_write_byte(uint16_t addr, uint8_t data) {
    i2c_start();
    i2c_write(EEPROM_ADDRESS << 1);  // Write mode
    i2c_write((uint8_t)(addr >> 8));  // Address high byte
    i2c_write((uint8_t)(addr & 0xFF));  // Address low byte
    i2c_write(data);  // Data
    i2c_stop();
    _delay_ms(10);  // Write cycle time (max 10ms for 24C64)
}

uint8_t eeprom_read_byte(uint16_t addr) {
    uint8_t data;
    
    // Set address pointer
    i2c_start();
    i2c_write(EEPROM_ADDRESS << 1);  // Write mode
    i2c_write((uint8_t)(addr >> 8));  // Address high byte
    i2c_write((uint8_t)(addr & 0xFF));  // Address low byte
    
    // Read data
    i2c_start();  // Repeated start
    i2c_write((EEPROM_ADDRESS << 1) | 0x01);  // Read mode
    data = i2c_read_nack();
    i2c_stop();
    
    return data;
}

void eeprom_write_page(uint16_t addr, uint8_t* data, uint8_t len) {
    if (len > EEPROM_PAGE_SIZE) len = EEPROM_PAGE_SIZE;
    
    i2c_start();
    i2c_write(EEPROM_ADDRESS << 1);
    i2c_write((uint8_t)(addr >> 8));
    i2c_write((uint8_t)(addr & 0xFF));
    
    for (uint8_t i = 0; i < len; i++) {
        i2c_write(data[i]);
    }
    
    i2c_stop();
    _delay_ms(10);  // Write cycle time
}

void eeprom_read_sequential(uint16_t addr, uint8_t* buffer, uint16_t len) {
    // Set address pointer
    i2c_start();
    i2c_write(EEPROM_ADDRESS << 1);
    i2c_write((uint8_t)(addr >> 8));
    i2c_write((uint8_t)(addr & 0xFF));
    
    // Sequential read
    i2c_start();  // Repeated start
    i2c_write((EEPROM_ADDRESS << 1) | 0x01);
    
    for (uint16_t i = 0; i < len - 1; i++) {
        buffer[i] = i2c_read_ack();
    }
    buffer[len - 1] = i2c_read_nack();  // Last byte with NACK
    
    i2c_stop();
}

