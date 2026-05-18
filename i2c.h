#ifndef I2C_H_
#define I2C_H_

#define F_CPU 11059200UL
#include <avr/io.h>
#include <util/delay.h>

#define I2C_SCL_FREQ 100000UL  // 100kHz I2C clock
#define TWBR_VAL ((F_CPU/I2C_SCL_FREQ)-16)/2

// I2C Status codes
#define TW_START 0x08
#define TW_REP_START 0x10
#define TW_MT_SLA_ACK 0x18
#define TW_MT_DATA_ACK 0x28
#define TW_MR_SLA_ACK 0x40
#define TW_MR_DATA_ACK 0x50
#define TW_MR_DATA_NACK 0x58

// Function prototypes
void i2c_init(void);
void i2c_start(void);
void i2c_stop(void);
void i2c_write(uint8_t data);
uint8_t i2c_read_ack(void);
uint8_t i2c_read_nack(void);
uint8_t i2c_get_status(void);

#endif
