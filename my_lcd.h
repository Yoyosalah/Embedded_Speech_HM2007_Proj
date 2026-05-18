#ifndef _MY_LCD
#define _MY_LCD

// Alphanumeric LCD functions
#define LCD_Dir  DDRD             /* Changed to PORTD */
#define LCD_Port PORTD            /* Changed to PORTD */
#define LCD_Pin  PIND             /* Changed to PORTD */

#define RS PORTD2                 /* Moved to PD2 */
#define EN PORTD3                 /* Moved to PD3 */
// RW pin is removed from code. Connect LCD Pin 5 physically to GND!

void LCD_Command( unsigned char cmnd );
void LCD_Char( unsigned char data );
void LCD_Init (void);
void LCD_String (char *str);
void LCD_String_xy (char row, char pos, char *str);
void LCD_Clear();
void lcd_create_char(unsigned char address, unsigned char pattern[]);
void LCD_Gotoxy(char row, char pos);

// Note: LCD_Read_Char is removed because we grounded RW

#endif