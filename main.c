/*****************************************************************************
 *   Value read from BNC is written to the OLED display (nothing graphical
 *   yet only value).
 *
 *   Copyright(C) 2010, Embedded Artists AB
 *   All rights reserved.
 *
 ******************************************************************************/

#include "lpc17xx_pinsel.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_i2c.h"
#include "lpc17xx_ssp.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_rtc.h"
#include "joystick.h"
#include "oled.h"
#include "pca9532.h"
#include "time.h"
#include "stdio.h"
#include "stdlib.h"
#include "lpc17xx_uart.h"
#include "lpc17xx_dac.h"
#include "system_LPC17xx.h"
#include "stdbool.h"
static uint8_t buf[10];

static void intToString(int value, uint8_t* pBuf, uint32_t len, uint32_t base)
{
    static const char* pAscii = "0123456789abcdefghijklmnopqrstuvwxyz";
    int pos = 0;
    int tmpValue = value;

    // the buffer must not be null and at least have a length of 2 to handle one
    // digit and null-terminator
    if (pBuf == NULL || len < 2)
    {
        return;
    }

    // a valid base cannot be less than 2 or larger than 36
    // a base value of 2 means binary representation. A value of 1 would mean only zeros
    // a base larger than 36 can only be used if a larger alphabet were used.
    if (base < 2 || base > 36)
    {
        return;
    }

    // negative value
    if (value < 0)
    {
        tmpValue = -tmpValue;
        value    = -value;
        pBuf[pos++] = '-';
    }

    // calculate the required length of the buffer
    do {
        pos++;
        tmpValue /= base;
    } while(tmpValue > 0);


    if (pos > len)
    {
        // the len parameter is invalid.
        return;
    }

    pBuf[pos] = '\0';

    do {
        pBuf[--pos] = pAscii[value % base];
        value /= base;
    } while(value > 0);

    return;
}

static void init_ssp(void)
{
	SSP_CFG_Type SSP_ConfigStruct;
	PINSEL_CFG_Type PinCfg;

	/*
	 * Initialize SPI pin connect
	 * P0.7 - SCK;
	 * P0.8 - MISO
	 * P0.9 - MOSI
	 * P2.2 - SSEL - used as GPIO
	 */
	PinCfg.Funcnum = 2;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Portnum = 0;
	PinCfg.Pinnum = 7;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 8;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 9;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Funcnum = 0;
	PinCfg.Portnum = 2;
	PinCfg.Pinnum = 2;
	PINSEL_ConfigPin(&PinCfg);

	SSP_ConfigStructInit(&SSP_ConfigStruct);

	// Initialize SSP peripheral with parameter given in structure above
	SSP_Init(LPC_SSP1, &SSP_ConfigStruct);

	// Enable SSP peripheral
	SSP_Cmd(LPC_SSP1, ENABLE);

}


static void init_i2c(void)
{
	PINSEL_CFG_Type PinCfg;

	/* Initialize I2C2 pin connect */
	PinCfg.Funcnum = 2;
	PinCfg.Pinnum = 10;
	PinCfg.Portnum = 0;
	PINSEL_ConfigPin(&PinCfg);
	PinCfg.Pinnum = 11;
	PINSEL_ConfigPin(&PinCfg);

	// Initialize I2C peripheral
	I2C_Init(LPC_I2C2, 100000);

	/* Enable I2C1 operation */
	I2C_Cmd(LPC_I2C2, ENABLE);
}

static void init_adc(void) //potrzebne, bez tego nie wyswietla sie data
{
	PINSEL_CFG_Type PinCfg;

	/*
	 * Init ADC pin connect
	 * AD0.5 on P1.31
	 */
	PinCfg.Funcnum = 3;
	PinCfg.OpenDrain = 0;
	PinCfg.Pinmode = 0;
	PinCfg.Pinnum = 31;
	PinCfg.Portnum = 1;
	PINSEL_ConfigPin(&PinCfg);

	/* Configuration for ADC :
	 * 	Frequency at 0,2Mhz
	 *  ADC channel 5, no Interrupt
	 */
	ADC_Init(LPC_ADC, 200000);
	ADC_IntConfig(LPC_ADC,ADC_CHANNEL_5,DISABLE);
	ADC_ChannelCmd(LPC_ADC,ADC_CHANNEL_5,ENABLE);

}

extern const unsigned char sound_8k[];
extern int sound_sz;

#define UART_DEV LPC_UART3

//static void init_uart(void)
//{
//	PINSEL_CFG_Type PinCfg;
//	UART_CFG_Type uartCfg;
//
//	/* Initialize UART3 pin connect */
//	PinCfg.Funcnum = 2;
//	PinCfg.Pinnum = 0;
//	PinCfg.Portnum = 0;
//	PINSEL_ConfigPin(&PinCfg);
//	PinCfg.Pinnum = 1;
//	PINSEL_ConfigPin(&PinCfg);
//
//	uartCfg.Baud_rate = 115200;
//	uartCfg.Databits = UART_DATABIT_8;
//	uartCfg.Parity = UART_PARITY_NONE;
//	uartCfg.Stopbits = UART_STOPBIT_1;
//
//	UART_Init(UART_DEV, &uartCfg);
//
//	UART_TxCmd(UART_DEV, ENABLE);
//
//}


int main (void) {

    uint8_t Thour= 12, Tminute = 36, Tday=5,Tmonth=4, Tsecond=0;
    uint8_t dir = 0;
    uint8_t Bhour = 0, Bminute = 0;
    uint8_t jPosition = 0;
    uint8_t jAlarm = 0;
    uint8_t joy = 0;

    uint16_t Tyear=2023;
    uint16_t ledOn = 0;
    uint16_t ledOff = 0;

    uint32_t val  = 0;
    uint32_t delay = 100;
    uint32_t cnt = 0;

    bool wcisniety = false;

    init_i2c();
    init_ssp();
    init_adc();

    RTC_Init(LPC_RTC);
    RTC_Cmd(LPC_RTC, ENABLE);

    //----------SEKCJA OLED-------------

    oled_init();

    oled_clearScreen(OLED_COLOR_WHITE);

    oled_putString(1,1,  (uint8_t*)"Data: ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1,11,  (uint8_t*)"Godzina: ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);
    oled_putString(1,41,  (uint8_t*)"Alarm: ", OLED_COLOR_BLACK, OLED_COLOR_WHITE);

    //----------KONIEC SEKCJI OLED-------------

    //-----------------------SEKCJA RTC------------------------------

    RTC_SetTime( LPC_RTC,  RTC_TIMETYPE_YEAR, Tyear);
    RTC_SetTime( LPC_RTC,  RTC_TIMETYPE_MONTH, Tmonth);
    RTC_SetTime( LPC_RTC,  RTC_TIMETYPE_DAYOFMONTH, Tday);
    RTC_SetTime( LPC_RTC,  RTC_TIMETYPE_HOUR, Thour);
    RTC_SetTime( LPC_RTC,  RTC_TIMETYPE_MINUTE, Tminute);

    //-----------------------KONIEC SEKCJI RTC------------------------------

    	PINSEL_CFG_Type PinCfg;

    	/* Initialize I2C2 pin connect */
    	PinCfg.Funcnum = 2;
    	PinCfg.Pinnum = 10;
    	PinCfg.Portnum = 0;
    	PINSEL_ConfigPin(&PinCfg);
    	PinCfg.Pinnum = 11;
    	PINSEL_ConfigPin(&PinCfg);

    	// Initialize I2C peripheral
    	I2C_Init(LPC_I2C2, 100000);

    	/* Enable I2C1 operation */
    	I2C_Cmd(LPC_I2C2, ENABLE);

    	pca9532_init();

        joystick_init();




        //---------------SEKCJA DAC-------------------------


        //PINSEL_CFG_Type PinCfg;

            uint32_t cntW = 0;
            uint32_t off = 0;
            uint32_t sampleRate = 0;
            //uint32_t delay = 0;

            GPIO_SetDir(2, 1<<0, 1);
            GPIO_SetDir(2, 1<<1, 1);

            GPIO_SetDir(0, 1<<27, 1);
            GPIO_SetDir(0, 1<<28, 1);
            GPIO_SetDir(2, 1<<13, 1);
            GPIO_SetDir(0, 1<<26, 1);

            GPIO_ClearValue(0, 1<<27); //LM4811-clk
            GPIO_ClearValue(0, 1<<28); //LM4811-up/dn
            GPIO_ClearValue(2, 1<<13); //LM4811-shutdn

        	/*
        	 * Init DAC pin connect
        	 * AOUT on P0.26
        	 */
        	PinCfg.Funcnum = 2;
        	PinCfg.OpenDrain = 0;
        	PinCfg.Pinmode = 0;
        	PinCfg.Pinnum = 26;
        	PinCfg.Portnum = 0;
        	PINSEL_ConfigPin(&PinCfg);

        	/* init DAC structure to default
        	 * Maximum	current is 700 uA
        	 * First value to AOUT is 0
        	 */
        	DAC_Init(LPC_DAC);

            //init_uart();


            /* ChunkID */
            if (sound_8k[cntW] != 'R' && sound_8k[cntW+1] != 'I' &&
                sound_8k[cntW+2] != 'F' && sound_8k[cntW+3] != 'F')
            {
            	UART_SendString(UART_DEV, (uint8_t*)"Wrong format (RIFF)\r\n");
                return 0;
            }
            cntW+=4;

            /* skip chunk size*/
            cntW += 4;

            /* Format */
            if (sound_8k[cntW] != 'W' && sound_8k[cntW+1] != 'A' &&
                sound_8k[cntW+2] != 'V' && sound_8k[cntW+3] != 'E')
            {
            	UART_SendString(UART_DEV, (uint8_t*)"Wrong format (WAVE)\r\n");
                return 0;
            }
            cntW+=4;

            /* SubChunk1ID */
            if (sound_8k[cntW] != 'f' && sound_8k[cntW+1] != 'm' &&
                sound_8k[cntW+2] != 't' && sound_8k[cntW+3] != ' ')
            {
            	UART_SendString(UART_DEV, (uint8_t*)"Missing fmt\r\n");
                return 0;
            }
            cntW+=4;

            /* skip chunk size, audio format, num channels */
            cntW+= 8;

            sampleRate = (sound_8k[cntW] | (sound_8k[cntW+1] << 8) |
                    (sound_8k[cntW+2] << 16) | (sound_8k[cntW+3] << 24));

            if (sampleRate != 8000) {
            	UART_SendString(UART_DEV, (uint8_t*)"Only 8kHz supported\r\n");
                return 0;
            }

            delay = 1000000 / sampleRate;

            cntW+=4;

            /* skip byte rate, align, bits per sample */
            cntW += 8;

            /* SubChunk2ID */
            if (sound_8k[cntW] != 'd' && sound_8k[cntW+1] != 'a' &&
                sound_8k[cntW+2] != 't' && sound_8k[cntW+3] != 'a')
            {
            	UART_SendString(UART_DEV, (uint8_t*)"Missing data\r\n");
                return 0;
            }
            cntW += 4;

            /* skip chunk size */
            cntW += 4;

            off = cntW;


            //-----------------------KONIEC SEKCJI DAC------------------------------



            //-----------------------SEKCJA RTC------------------------------
    while(1) {
    	Tsecond= RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_SECOND);

        /* analog input connected to BNC */
    	ADC_StartCmd(LPC_ADC,ADC_START_NOW);
    	//Wait conversion complete
    	while (!(ADC_ChannelGetStatus(LPC_ADC,ADC_CHANNEL_5,ADC_DATA_DONE)));
    	val = ADC_ChannelGetData(LPC_ADC,ADC_CHANNEL_5);

        /* output values to OLED display */
/*
        intToString(Tyear, buf, 10, 10);
        oled_fillRect((1+6*6),1, 80, 8, OLED_COLOR_WHITE);
        oled_putString((1+9*6),1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);
*/

    	if (Tyear != RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_YEAR))
    	{
    		Tyear = RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_YEAR);
    	}

    	if (Tmonth != RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_MONTH))
    	 {
    	    		Tmonth = RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_MONTH);
    	 }

    	if (Tday != RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_DAYOFMONTH))
    	 {
    	    		Tday = RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_DAYOFMONTH);
    	 }

    	if (Thour != RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_HOUR))
    	    	{
    	    		Thour = RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_HOUR);
    	    	}

    	if (Tminute != RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_MINUTE))
    	    	{
    	    		Tminute = RTC_GetTime( LPC_RTC,  RTC_TIMETYPE_MINUTE);
    	    	}

    	intToString(Tyear, buf, 10, 10);
    	oled_putString((32),1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        intToString(Tmonth, buf, 10, 10);
        oled_putString((62),1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);


        intToString(Tday, buf, 10, 10);
        oled_putString((75),1, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        intToString(Thour, buf, 10, 10);
        oled_putString((44),11, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        snprintf(buf, 10, "%02d", Tminute);
        oled_putString((59),11, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        snprintf(buf, 10, "%02d", Tsecond);
        oled_putString((73),11, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        snprintf(buf, 10, "%02d", Bhour);
        oled_putString((59),41, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);

        snprintf(buf, 10, "%02d", Bminute);
        oled_putString((73),41, buf, OLED_COLOR_BLACK, OLED_COLOR_WHITE);


  //-----------------------KONIEC SEKCJI RTC------------------------------


//--------------------SEKCJA DŻOJSTIKA--------------------


        joy = joystick_read();


               if (((joy & JOYSTICK_CENTER) != 0) ) {

            	   wcisniety = true;
               }

               if ((wcisniety && (joy & JOYSTICK_CENTER) == 0)) {
/*  tu nastapią zmiany */
            	   wcisniety = false;
                   if (jAlarm == 0){
                	   jAlarm = 1;
                   }else{
                	   jAlarm=0;
                   }
               }

               if ((joy & JOYSTICK_DOWN) != 0) {
            	   if(jPosition == 1){
            	  Bminute--;
            	  if(Bminute == 255){
            	  Bminute = 59;
            	  }
            	}if(jPosition == 0)
            	{
            		Bhour--;
            		if(Bhour == 255 ){
            		Bhour = 23;
            	}

                   if (delay > 30)
                       delay -= 10;
               }
               }

               if ((joy & JOYSTICK_UP) != 0) {

            	   if(jPosition == 1){

            	   Bminute++;
            	   if(Bminute == 60){

            	   Bminute = 0;
            	  }

            	  }if(jPosition == 0)
            	  {

            	   Bhour++;
            	    if(Bhour == 24){
            	    Bhour = 0;
            	  }

            	   if (delay > 30)
            	   delay -= 10;
            	 }
               }

               if ((joy & JOYSTICK_LEFT) != 0) {
            	   jPosition = 0;
                   dir = 0;
               }

               if ((joy & JOYSTICK_RIGHT) != 0) {
            	   jPosition = 1;
                   dir = 1;
               }




               if (dir) {
                   if (cnt == 0)
                       cnt = 31;
                   else
                       cnt--;

               } else {
                   cnt++;
                   if (cnt >= 32)
                       cnt = 0;
               }
               //--------------------KONIEC SEKCJI DŻOJSTIKA--------------------

               //--------------------SEKCJA TIMERA--------------------

               Timer0_Wait(delay);
               if((Thour == Bhour)&(Tminute == Bminute)&(jAlarm == 1)){



            	    	  if (cnt < 16)
            	    	     ledOn |= (1 << cnt);
            	    	  if (cnt > 15)
            	    	     ledOn &= ~( 1 << (cnt - 10) );

            	    	  if (cnt > 15)
            	    	          ledOff |= ( 1 << (cnt - 15) );
            	    	  if (cnt < 16)
            	    	          ledOff &= ~(1 << cnt);
            	    	    pca9532_setLeds(ledOn, ledOff);


            	    	           cntW = off;

            	    	           while(cntW++ < sound_sz)
            	    	           {

            	    	           	DAC_UpdateValue ( LPC_DAC,(uint32_t)(sound_8k[cntW]));
            	    	           	Timer0_us_Wait(delay);

            	    	        	   if(jAlarm == 0){
            	    	        		   break;
            	    	        	   }
            	    	           }


            	    	       }







                  }







        Timer0_Wait(1000);

        //--------------------KONIEC SEKCJI TIMERA--------------------

    }






void check_failed(uint8_t *file, uint32_t line)
{
	/* User can add his own implementation to report the file name and line number,
	 ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

	/* Infinite loop */
	while(1);
}
