#ifndef F_CPU
#define F_CPU 11059200UL
#endif

#define USART_BAUDRATE 9600
#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdint.h>
#include "my_lcd.h"
#include "i2c.h"
#include "24c64.h"

// --- AUDIO SYSTEM DEFINES ---
#define NUM_FRAMES        40
#define SAMPLES_PER_FRAME 200
#define NUM_FEATURES      4

#define START_THRESHOLD      50
#define END_THRESHOLD        25
#define SILENCE_FRAMES       12
#define PREROLL_FRAMES       2
#define REJECTION_THRESHOLD  6000
#define NOISE_FLOOR          40
#define DTW_BAND_DIVISOR     3
#define SSC_SHIFT            1

// --- HARDWARE INTERFACE DEFINES ---
#define BTN_MODE   (1 << PA0)
#define BTN_TRAIN  (1 << PA1)
#define LED_DETECT (1 << PA2)
#define LED_RECORD (1 << PA3)
#define BTN_CLEAR  (1 << PA4)

#define MODE_DETECT  0
#define MODE_RECORD  1
volatile uint8_t system_mode = MODE_DETECT;

uint8_t target_template_idx = 1;

#define STATE_IDLE       0
#define STATE_RECORDING  1
#define STATE_PROCESSING 2
volatile uint8_t current_state = STATE_IDLE;

// ==========================================
// GLOBALS
// ==========================================
volatile uint8_t live_features[NUM_FRAMES][NUM_FEATURES];
uint8_t          temp_template[NUM_FRAMES][NUM_FEATURES];

volatile uint8_t  current_frame = 0;
volatile uint16_t sample_count  = 0;

volatile int32_t energy_accumulator = 0;
volatile uint8_t zcr_accumulator    = 0;
volatile uint8_t ssc_accumulator    = 0;
volatile int16_t last_sample        = 0;
volatile int8_t  last_slope         = 0;

volatile int16_t ser_low_state   = 0;
volatile int32_t low_energy_acc  = 0;
volatile int32_t high_energy_acc = 0;

volatile uint8_t silence_counter = 0;
volatile uint8_t noise_counter   = 0;

// Simple boolean trigger: 1 = update LCD, 0 = do nothing
volatile uint8_t speech_started_flag = 0;

// ==========================================
// UART SETUP
// ==========================================
void UART_init(void) {
    UBRRL = BAUD_PRESCALE;
    UBRRH = (BAUD_PRESCALE >> 8);
    UCSRB = (1 << RXEN) | (1 << TXEN);
    UCSRC = (1 << URSEL) | (1 << UCSZ0) | (1 << UCSZ1);
}

int UART_getChar(FILE *stream) {
    while ((UCSRA & (1 << RXC)) == 0);
    return UDR;
}

int UART_putChar(char c, FILE *stream) {
    while (!(UCSRA & (1 << UDRE)));
    UDR = c;
    return 0;
}
static FILE uart_str = FDEV_SETUP_STREAM(UART_putChar, UART_getChar, _FDEV_SETUP_RW);

// ==========================================
// ADC & TIMER SETUP
// ==========================================
void adc_init(void) {
    ADMUX  = (1 << REFS0) | 0x05;
    ADCSRA = (1 << ADEN) | (1 << ADATE) | (1 << ADSC) | (1 << ADPS2) | (1 << ADPS1);
}

void timer1_init(void) {
    TCCR1B  = 0;
    TCCR1B |= (1 << WGM12) | (1 << CS10);
    OCR1A   = 1381;
    TIMSK  |= (1 << OCIE1A);
}

// ==========================================
// EEPROM HANDLERS
// ==========================================
void SaveTemplateToEEPROM(uint8_t index, uint8_t frames) {
    uint16_t base_addr = (index - 1) * 256;
    eeprom_write_byte(base_addr, frames);

    uint16_t data_addr = base_addr + 1;
    for (int i = 0; i < frames; i++) {
        for (int j = 0; j < NUM_FEATURES; j++) {
            eeprom_write_byte(data_addr, live_features[i][j]);
            data_addr++;
        }
    }
}

uint8_t LoadTemplateFromEEPROM(uint8_t index) {
    uint16_t base_addr = (index - 1) * 256;
    uint8_t frames = eeprom_read_byte(base_addr);

    if (frames == 0 || frames > NUM_FRAMES) return 0;

    eeprom_read_sequential(base_addr + 1, (uint8_t*)temp_template, frames * NUM_FEATURES);
    return frames;
}

// ==========================================
// ISR: AUDIO SAMPLING & VAD
// ==========================================
ISR(TIMER1_COMPA_vect) {
    int16_t current_sample = ADC - 512;

    energy_accumulator += abs(current_sample);
    ser_low_state = ser_low_state - (ser_low_state >> 2) + (current_sample >> 2);

    if (abs(current_sample) > NOISE_FLOOR) {
        int16_t ser_high = current_sample - ser_low_state;
        low_energy_acc  += abs(ser_low_state);
        high_energy_acc += abs(ser_high);
    }

    if (abs(current_sample) > NOISE_FLOOR || abs(last_sample) > NOISE_FLOOR) {
        if ((current_sample >= 0 && last_sample < 0) ||
            (current_sample < 0  && last_sample >= 0)) {
            zcr_accumulator++;
        }
        int8_t current_slope = (current_sample >= last_sample) ? 1 : -1;
        if (current_slope != last_slope) {
            ssc_accumulator++;
        }
        last_slope = current_slope;
    }

    last_sample = current_sample;
    sample_count++;

    if (sample_count >= SAMPLES_PER_FRAME) {
        uint8_t frame_energy = (uint8_t)(energy_accumulator / SAMPLES_PER_FRAME);
        uint8_t frame_zcr    = zcr_accumulator;
        uint8_t frame_ssc    = ssc_accumulator >> SSC_SHIFT;

        uint32_t total_band = low_energy_acc + high_energy_acc;
        uint8_t  frame_ser  = (total_band > 0)
                              ? (uint8_t)((high_energy_acc * 255UL) / total_band)
                              : 128;

        if (current_state == STATE_IDLE) {
            if (frame_energy > START_THRESHOLD) {
                live_features[noise_counter][0] = frame_energy;
                live_features[noise_counter][1] = frame_zcr;
                live_features[noise_counter][2] = frame_ssc;
                live_features[noise_counter][3] = frame_ser;

                noise_counter++;
                if (noise_counter >= PREROLL_FRAMES) {
                    // Trigger the UI safely
                    speech_started_flag = 1;
                    
                    current_state = STATE_RECORDING;
                    current_frame = PREROLL_FRAMES;
                    noise_counter = 0; 
                }
            } else {
                noise_counter = 0;
            }
        }
        else if (current_state == STATE_RECORDING) {
            live_features[current_frame][0] = frame_energy;
            live_features[current_frame][1] = frame_zcr;
            live_features[current_frame][2] = frame_ssc;
            live_features[current_frame][3] = frame_ser;
            current_frame++;

            if (frame_energy < END_THRESHOLD) {
                silence_counter++;
            } else {
                silence_counter = 0;
            }

            if (silence_counter >= SILENCE_FRAMES || current_frame >= NUM_FRAMES) {
                if (current_frame > silence_counter) {
                    current_frame -= silence_counter;
                }
                current_state   = STATE_PROCESSING;
                silence_counter = 0;
                noise_counter   = 0;
            }
        }

        sample_count       = 0;
        energy_accumulator = 0;
        zcr_accumulator    = 0;
        ssc_accumulator    = 0;
        low_energy_acc     = 0;
        high_energy_acc    = 0;
    }
}

// ==========================================
// DTW
// ==========================================
uint16_t calculate_dtw_ram(uint8_t tmpl_frames, uint8_t actual_frames) {
    uint16_t row_prev[NUM_FRAMES + 1];
    uint16_t row_curr[NUM_FRAMES + 1];

    uint8_t longer  = (actual_frames > tmpl_frames) ? actual_frames : tmpl_frames;
    uint8_t shorter = (actual_frames < tmpl_frames) ? actual_frames : tmpl_frames;

    if (longer > (uint16_t)(shorter * 2) + 6) return 65535;

    uint8_t band = longer / DTW_BAND_DIVISOR;
    if (band < 2) band = 2;

    for (int i = 0; i <= tmpl_frames; i++) row_prev[i] = 65535;
    row_prev[0] = 0;

    for (int i = 1; i <= actual_frames; i++) {
        row_curr[0] = 65535;
        for (int j = 1; j <= tmpl_frames; j++) {
            int diag = (i * tmpl_frames) / actual_frames;
            if (abs(j - diag) > (int)band) {
                row_curr[j] = 65535;
                continue;
            }

            uint8_t tmpl_energy = temp_template[j-1][0];
            uint8_t tmpl_zcr    = temp_template[j-1][1];
            uint8_t tmpl_ssc    = temp_template[j-1][2];
            uint8_t tmpl_ser    = temp_template[j-1][3];

            int16_t live_delta_energy = (i > 1)
                ? ((int16_t)live_features[i-1][0] - (int16_t)live_features[i-2][0]) : 0;
            int16_t tmpl_delta_energy = (j > 1)
                ? ((int16_t)tmpl_energy - (int16_t)temp_template[j-2][0]) : 0;

            uint16_t energy_dist = abs(live_delta_energy - tmpl_delta_energy);
            uint16_t zcr_dist    = abs((int16_t)live_features[i-1][1] - tmpl_zcr);
            uint16_t ssc_dist    = abs((int16_t)live_features[i-1][2] - tmpl_ssc);
            uint16_t ser_dist    = abs((int16_t)live_features[i-1][3] - tmpl_ser);

            uint16_t dist = (energy_dist * 3) + (zcr_dist * 4) +
                            (ssc_dist    * 3) + (ser_dist  * 2);

            uint16_t min_path = row_prev[j];
            if (row_curr[j-1] < min_path) min_path = row_curr[j-1];
            if (row_prev[j-1] < min_path) min_path = row_prev[j-1];

            if (min_path == 65535) {
                row_curr[j] = 65535;
            } else {
                uint32_t total = (uint32_t)dist + min_path;
                row_curr[j] = (total > 65534) ? 65535 : (uint16_t)total;
            }
        }
        for (int k = 0; k <= tmpl_frames; k++) row_prev[k] = row_curr[k];
    }
    return row_curr[tmpl_frames];
}

// ==========================================
// MAIN LOOP
// ==========================================
int main(void) {
    UART_init();
    stdin = stdout = &uart_str;

    adc_init();
    timer1_init();
    i2c_init();

    LCD_Init();
    LCD_Clear();

    DDRA &= ~(BTN_MODE | BTN_TRAIN | BTN_CLEAR);
    PORTA |= (BTN_MODE | BTN_TRAIN | BTN_CLEAR);
    DDRA  |= (LED_DETECT | LED_RECORD);

    PORTA |= LED_DETECT;
    PORTA &= ~LED_RECORD;
    LCD_String("Mode: DETECT");
    LCD_Gotoxy(1, 0);
    LCD_String("Listening...");
    printf("=== System Ready (Detect) ===\r\n");

    sei();

    while (1) {

        // -------------------------------------------------------
        // UI: speech-start indicator
        // Reads system_mode directly. 100% immune to being wrong.
        // -------------------------------------------------------
        if (speech_started_flag) {
            speech_started_flag = 0;
            LCD_Clear();
            LCD_String("Speaking...");
        }

        // -------------------------------------------------------
        // BUTTON: MODE TOGGLE ? idle only
        // -------------------------------------------------------
        if (!(PINA & BTN_MODE)) {
            _delay_ms(50);
            if (!(PINA & BTN_MODE) && current_state == STATE_IDLE) {
                system_mode = (system_mode == MODE_DETECT) ? MODE_RECORD : MODE_DETECT;
                LCD_Clear();
                if (system_mode == MODE_DETECT) {
                    PORTA |= LED_DETECT;
                    PORTA &= ~LED_RECORD;
                    LCD_String("Mode: DETECT");
                    LCD_Gotoxy(1, 0);
                    LCD_String("Listening...");
                    printf("Mode: DETECT\r\n");
                } else {
                    PORTA |= LED_RECORD;
                    PORTA &= ~LED_DETECT;
                    char buf[20];
                    sprintf(buf, "Train Word: %d", target_template_idx);
                    LCD_String(buf);
                    LCD_Gotoxy(1, 0);
                    LCD_String("Listening...");
                    printf("Mode: RECORD (Word %d)\r\n", target_template_idx);
                }
            }
            while (!(PINA & BTN_MODE));
        }

        // -------------------------------------------------------
        // BUTTON: CYCLE TEMPLATE ? RECORD MODE ONLY
        // -------------------------------------------------------
        if (system_mode == MODE_RECORD && !(PINA & BTN_TRAIN)) {
            _delay_ms(50);
            if (system_mode == MODE_RECORD && !(PINA & BTN_TRAIN)) {
                target_template_idx++;
                if (target_template_idx > 8) target_template_idx = 1;
                char buf[20];
                sprintf(buf, "Train Word: %d", target_template_idx);
                LCD_Clear();
                LCD_String(buf);
                LCD_Gotoxy(1, 0);
                LCD_String("Listening...");
                printf("Target Word: %d\r\n", target_template_idx);
            }
            while (!(PINA & BTN_TRAIN));
        }

        // -------------------------------------------------------
        // BUTTON: CLEAR ? RECORD MODE ONLY, triple-gated
        // -------------------------------------------------------
        if (system_mode == MODE_RECORD && !(PINA & BTN_CLEAR)) {
            _delay_ms(50);
            if (system_mode != MODE_RECORD) {
                while (!(PINA & BTN_CLEAR));
            } else if (!(PINA & BTN_CLEAR)) {
                uint16_t hold_ms = 0;
                while (!(PINA & BTN_CLEAR) && hold_ms < 1000) {
                    _delay_ms(10);
                    hold_ms += 10;
                }
                if (hold_ms >= 1000 && system_mode == MODE_RECORD) {
                    LCD_Clear();
                    LCD_String("Clearing all...");
                    printf("Clearing all templates...\r\n");
                    for (uint8_t i = 1; i <= 8; i++) {
                        uint16_t base_addr = (i - 1) * 256;
                        eeprom_write_byte(base_addr, 0xFF);
                    }
                    LCD_Clear();
                    LCD_String("All Cleared!");
                    printf("All templates cleared.\r\n");
                    _delay_ms(1500);
                }
                // Always restore RECORD screen on exit
                if (system_mode == MODE_RECORD) {
                    LCD_Clear();
                    char buf[20];
                    sprintf(buf, "Train Word: %d", target_template_idx);
                    LCD_String(buf);
                    LCD_Gotoxy(1, 0);
                    LCD_String("Listening...");
                }
                while (!(PINA & BTN_CLEAR));
            }
        }

        // -------------------------------------------------------
        // AUDIO PROCESSING
        // -------------------------------------------------------
        if (current_state == STATE_PROCESSING) {
            cli();

            uint8_t snapshot_mode = system_mode; // single atomic byte read

            if (snapshot_mode == MODE_RECORD) {
                // --- RECORD PATH ---
                LCD_Clear();
                LCD_String("Saving...");
                printf("Saving %d frames to EEPROM Word %d...\r\n",
                       current_frame, target_template_idx);

                SaveTemplateToEEPROM(target_template_idx, current_frame);

                LCD_Clear();
                char buf[20];
                sprintf(buf, "Word %d Saved!", target_template_idx);
                LCD_String(buf);
                printf("Save Complete!\r\n");
                _delay_ms(1500);

                LCD_Clear();
                sprintf(buf, "Train Word: %d", target_template_idx);
                LCD_String(buf);
                LCD_Gotoxy(1, 0);
                LCD_String("Listening...");

            } else {
                // --- DETECT PATH ---
                LCD_Clear();
                LCD_String("Processing...");

                uint16_t best_score = 65535;
                uint8_t  best_match = 0;

                for (uint8_t i = 1; i <= 8; i++) {
                    uint8_t loaded_frames = LoadTemplateFromEEPROM(i);
                    if (loaded_frames == 0) continue;
                    uint16_t score = calculate_dtw_ram(loaded_frames, current_frame);
                    if (score < best_score) {
                        best_score = score;
                        best_match = i;
                    }
                }

                LCD_Clear();
                if (best_score < REJECTION_THRESHOLD && best_match > 0) {
                    char buf[20];
                    sprintf(buf, "Matched: Word %d", best_match);
                    LCD_String("Command:");
                    LCD_Gotoxy(1, 0);
                    LCD_String(buf);
                    printf("Match: Word %d (Score: %u)\r\n", best_match, best_score);
                } else {
                    LCD_String("Unknown Noise");
                    printf("Rejected (Best Score: %u)\r\n", best_score);
                }
                _delay_ms(1500);

                // Hardcoded restoration for detect mode
                LCD_Clear();
                LCD_String("Mode: DETECT");
                LCD_Gotoxy(1, 0);
                LCD_String("Listening...");
            }

            // Clean reset ? all counters zeroed before ISR re-enabled
            current_frame       = 0;
            noise_counter       = 0;
            silence_counter     = 0;
            speech_started_flag = 0;
            current_state       = STATE_IDLE;
            sei();
        }
    }
}