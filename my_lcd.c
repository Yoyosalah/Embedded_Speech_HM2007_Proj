#define F_CPU 11059200UL

#include <avr/io.h>
#include <util/delay.h>
#include "my_lcd.h"

void LCD_Command( unsigned char cmnd )
{
    LCD_Port = (LCD_Port & 0x0F) | (cmnd & 0xF0); /* sending upper nibble */
    LCD_Port &= ~ (1<<RS);        /* RS=0, command reg. */
    LCD_Port |= (1<<EN);          /* Enable pulse */
    _delay_us(1);
    LCD_Port &= ~ (1<<EN);

    _delay_us(200);

    LCD_Port = (LCD_Port & 0x0F) | (cmnd << 4);  /* sending lower nibble */
    LCD_Port |= (1<<EN);
    _delay_us(1);
    LCD_Port &= ~ (1<<EN);
    _delay_ms(2);
}

void LCD_Char( unsigned char data )
{
    LCD_Port = (LCD_Port & 0x0F) | (data & 0xF0); /* sending upper nibble */
    LCD_Port |= (1<<RS);        /* RS=1, data reg. */    
    
    LCD_Port|= (1<<EN);
    _delay_us(1);
    LCD_Port &= ~ (1<<EN);

    _delay_us(200);

    LCD_Port = (LCD_Port & 0x0F) | (data << 4); /* sending lower nibble */
    LCD_Port |= (1<<EN);
    _delay_us(1);
    LCD_Port &= ~ (1<<EN);
    _delay_ms(2);
}

void LCD_Init (void)
{
    // Important: We only want to set PD2 through PD7 as outputs.
    // We leave PD0 and PD1 alone so we don't break UART!
    LCD_Dir |= 0xFC;            /* Make PD2-PD7 outputs */
    _delay_ms(20);              /* LCD Power ON delay */
    
    LCD_Command(0x02);        /* send for 4 bit initialization of LCD  */
    LCD_Command(0x28);        /* 2 line, 5*7 matrix in 4-bit mode */
    LCD_Command(0x0c);        /* Display on cursor off*/
    LCD_Command(0x06);        /* Increment cursor */
    LCD_Command(0x01);        /* Clear display screen*/
    _delay_ms(2); 
}

void LCD_String (char *str)
{
    int i;
    for(i=0;str[i]!=0;i++)
    {
        LCD_Char (str[i]);
    }
}

void LCD_String_xy (char row, char pos, char *str)
{
    if (row == 0 && pos<16)
        LCD_Command((pos & 0x0F)|0x80);
    else if (row == 1 && pos<16)
        LCD_Command((pos & 0x0F)|0xC0);
    LCD_String(str);
}

void LCD_Clear()
{
    LCD_Command (0x01);
    _delay_ms(2);
    LCD_Command (0x80);
}

void lcd_create_char(unsigned char address, unsigned char pattern[])
{   
    if(address < 64 || address > 128)  return;
    LCD_Command(address);
    LCD_String((char *)pattern);
}

void LCD_Gotoxy(char row, char pos)
{    
    if(row ==0 && pos <16 )
        LCD_Command((pos & 0x0F) | 0x80);
    else if( row ==1 && pos < 16)
        LCD_Command((pos & 0x0F) | 0xC0);
}
