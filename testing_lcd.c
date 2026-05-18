#define F_CPU 11059200UL

#include <avr/io.h>
#include <util/delay.h>
#include "my_lcd.h" // Ensure this has the new PORTD definitions we made

int main(void) {
    // 1. Initialize the LCD
    // (This will automatically set PD2-PD7 as outputs while keeping PD0/PD1 safe)
    LCD_Init();
    
    // 2. Clear screen just in case
    LCD_Clear();
    
    // 3. Print to the first line
    LCD_String("PORTD LCD Test");
    
    // 4. Move to the second line (row 1, position 0)
    LCD_Gotoxy(1, 0);
    
    // 5. Print a success message
    LCD_String("It works!");
    
    // 6. Blink an indicator on the screen just to prove the code is running
    while (1) {
        LCD_Gotoxy(1, 14);
        LCD_String(" *");
        _delay_ms(500);
        
        LCD_Gotoxy(1, 14);
        LCD_String("  ");
        _delay_ms(500);
    }
    
    return 0;
}