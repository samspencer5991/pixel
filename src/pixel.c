/*
 * ws2812_driver.c
 *
 *  Created on: 19Apr.,2019
 *      Author: samspencer
 */

/*
 * clocked cycle = 208ns
 * bit cycle ~ 1250ns
 * reset = 9000ns
 *
 * Reportedly, the WS2812B is slightly slower than the regular WS2812
 * To ensure that this driver works for both, timing is biased slightly higher
 *
 * Running the SPI peripheral at 2.25MHz results in a typical pulse length of 444ns.
 * 	T0H: 200 < 350 < 500 - 2 bits
 * 	T0L: 550 < 700 < 850 - 3 bits
 * 	T1H: 650 < 800 < 950	- 4 bits
 * 	T1L: 450 < 600 < 750 - 3 bits
 *
 * 	'0' bit = 100
 * 	'1' bit = 110
 *
 */

#include "pixel.h"
#include "gpio.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HIGH_BIT 0b110
#define LOW_BIT 0b100

#define DATA_PIN GPIO_PIN_15
#define DATA_PORT GPIOB



// Adafruit's WS2812 Gamma Reduction Table
const uint8_t gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

  uint8_t sk6812gamma8[256];

// Local function prototypes
void setPixelSpi(PixelDriver* leds, uint16_t num, uint32_t colour);
ArgbErrorState showSpi(PixelDriver* leds);
void show(PixelDriver* leds);
void create_sk6812_gamma_table(float gamma, float brightness, uint8_t table[256]);

// Global functions
/**
  * @brief Initialise default starts on startup for LEDs using unified mode
  * @param leds	structure for the PixelDriver.
  * 					Members must already be assigned prior to calling this function.
  * 					This function can also be called to clear all LED colours
  * @retval none
  */
void pixel_Init(PixelDriver* leds)
{
	// General
	for(int i=0; i<leds->numPixels; i++)
	{
		leds->pixelBuffer[i] = OFF;
	}

	// SPI specific
	for(uint16_t i=0; i<PIXEL_BUFFER_SIZE_SPI(leds->numPixels); i++)
	{
		leds->altPixelBuffer[i] = 0x00;
	}

	// Populate gamma LUTs
	create_sk6812_gamma_table(2.8f, 1.0f, sk6812gamma8);
}


/**
  * @brief Returns a 24-bit colour value scaled by the brightness value
  * @param colour			24-bit colour reference value
  * @param brightness	0-255 (8-bit) scaling factor
  * @retval none
  */
uint32_t pixel_ScaleColour(uint32_t colour, uint8_t brightness)
{
	uint8_t r = ((colour>>16) & 0xff);
	uint8_t g = ((colour>>8) & 0xff);
	uint8_t b = (colour & 0xff);
	if(brightness != 255)
	{
		float rChunk = (float)r / 255.00;
		float gChunk = (float)g / 255.00;
		float bChunk = (float)b / 255.00;

		r = rChunk * brightness;
		g = gChunk * brightness;
		b = bChunk * brightness;
	}
	return (r<<16) + (g<<8) + b;
}

/**
  * @brief Helper function to clear all LEDs. Note that show_SPI must be called to clear
  * @param leds	structure for the PixelDriver.
  * @retval none
  */
void pixel_Clear(PixelDriver* leds)
{
	if(leds->protocol == LedGpio || leds->protocol == LedPwm)
	{
		for(int i=0; i < leds->numPixels; i++)
		{
			leds->pixelBuffer[i] = OFF;
		}
	}
	else if(leds->protocol == LedSpi)
	{
		for(int i=0; i < leds->numPixels; i++)
		{
			setPixelSpi(leds, i, OFF);
		}
	}
}

/**
  * @brief Checks the colour mode and assigns pixel number (num) a 24-bit colour value (colour)
  * @param leds	structure for the PixelDriver.
  * @param num		pixel number to set
  * @param colour	24-bit colour to assign to pixel
  * @retval none
  */
void pixel_SetPixel(PixelDriver* leds, uint16_t index, uint32_t colour)
{
	if(leds->protocol == LedGpio || leds->protocol == LedPwm)
	{
		uint32_t outputColour = 0x00000000;
		uint8_t r = 0;
		uint8_t g = 0;
		uint8_t b = 0;
		if(index < leds->numPixels)
		{
			if(leds->colourMode == ORDER_RGB)
			{
				b = gamma8[(colour) & 0xff];
				g = gamma8[(colour >> 8) & 0xff];
				r = gamma8[(colour >> 16) & 0xff];
			}
			else if(leds->colourMode == ORDER_GBR)
			{
				r = gamma8[(colour) & 0xff];
				b = gamma8[(colour >> 8) & 0xff];
				g = gamma8[(colour >> 16) & 0xff];
			}
			else if(leds->colourMode == ORDER_RBG)
			{
				g = gamma8[(colour) & 0xff];
				b = gamma8[(colour >> 8) & 0xff];
				r = gamma8[(colour >> 16) & 0xff];
			}

			// Scale colour channels according to set brightness
			if(leds->brightness != 255)
			{
				float rChunk = (float)r / 255.00;
				float gChunk = (float)g / 255.00;
				float bChunk = (float)b / 255.00;

				r = rChunk * leds->brightness;
				g = gChunk * leds->brightness;
				b = bChunk * leds->brightness;
			}
			
			outputColour = g | (r<<8) | (b<<16);
			leds->pixelBuffer[index] = outputColour;


			
		}
	}
	else if(leds->protocol == LedSpi)
	{
		setPixelSpi(leds, index, colour);
	}
}

ArgbErrorState pixel_Show(PixelDriver* leds)
{
	if(leds->protocol == LedPwm)
	{
		uint32_t index = 0;
		uint32_t tempColor;
		for (uint16_t i= 0; i<leds->numPixels; i++)
		{
			tempColor = leds->pixelBuffer[i];

			for (int j=23; j>=0; j--)
			{
				if (tempColor&(1<<j))
					leds->altPixelBuffer[index] = leds->pwmHighThreshold;		// 2/3 of ARR

				else
					leds->altPixelBuffer[index] = leds->pwmLowThreshold;		// 1/3 of ARR

				index++;
			}
		}

		for (uint8_t i=0; i<50; i++)
		{
			leds->altPixelBuffer[index] = 0;
			index++;
		}
		leds->htim->Instance->CCR2 = 0;
		leds->htim->Instance->CNT = 0;
		leds->ready = 0;
		if(HAL_TIM_PWM_Start_DMA(leds->htim, leds->timChannel, (uint32_t*)leds->altPixelBuffer, index) != HAL_OK)
			return ArgbHalError;
		else
 			return ArgbOk;
		
	}
	else if(leds->protocol == LedSpi)
	{
		return showSpi(leds);
	}
	return ArgbOk;
}


// Local functions
/**
  * @brief Checks the colour mode and assigns pixel number (num) a 24-bit colour value (colour)
  * @param num		pixel number to set
  * @param colour	24-bit colour to assign to pixel
  * @retval none
  */
void setPixelSpi(PixelDriver* leds, uint16_t pixel, uint32_t colour)
{
	uint16_t num = pixel*9;
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	unsigned long g24 = 0;
	unsigned long r24 = 0;
	unsigned long b24 = 0;

	if(pixel < leds->numPixels)
	{
		//depending on the set order, extract each colour channel
		// Gamma correct is now post brightness scaling for better linearity
		if(leds->colourMode == ORDER_RGB)
		{
			r = (colour >> 16) & 0xff;
			g = (colour >> 8) & 0xff;
			b = (colour) & 0xff;
		}
		else if(leds->colourMode == ORDER_BRG)
		{
			r = (colour >> 8) & 0xff;
			g = (colour) & 0xff;
			b = (colour >> 16) & 0xff;
		}
		else if(leds->colourMode == ORDER_GBR)
		{
			r = (colour) & 0xff;
			g = (colour >> 16) & 0xff;
			b = (colour >> 8) & 0xff;
		}

		// Scale colour channels according to set brightness
		// Check if a brightness has been set
		// Gamma correction now post brightnes
		if(leds->brightness != 255)
		{
			float rChunk = (float)r / 255.00;
			float gChunk = (float)g / 255.00;
			float bChunk = (float)b / 255.00;

			r = rChunk * leds->brightness;
			g = gChunk * leds->brightness;
			b = bChunk * leds->brightness;
		}
		r = gamma8[r];
		g = gamma8[g];
		b = gamma8[b];

		//create 24-bit data (3x8) filled value and then split into bytes
		for(int i=0; i<8; i++)
		{
			if(((g>>i) & 1) == 1)
			{
				g24 |= (1<<(i*3+2))|(1<<(i*3+1));
			}
			else
			{
				g24 |= (1<<(i*3+2));
			}
			if(((r>>i) & 1) == 1)
			{
				r24 |= (1<<(i*3+2))|(1<<(i*3+1));
			}
			else
			{
				r24 |= (1<<(i*3+2));
			}
			if(((b>>i) & 1) == 1)
			{
				b24 |= (1<<(i*3+2))|(1<<(i*3+1));
			}
			else
			{
				b24 |= (1<<(i*3+2));
			}
		}

		//fill the SPI buffer from the RGB channel contents
		leds->altPixelBuffer[num] = (g24 >> 16) & 0xff;
		leds->altPixelBuffer[num+1] = (g24 >> 8) & 0xff;
		leds->altPixelBuffer[num+2] = (g24) & 0xff;

		leds->altPixelBuffer[num+3] = (r24 >> 16) & 0xff;
		leds->altPixelBuffer[num+4] = (r24 >> 8) & 0xff;
		leds->altPixelBuffer[num+5] = (r24) & 0xff;

		leds->altPixelBuffer[num+6] = (b24 >> 16) & 0xff;
		leds->altPixelBuffer[num+7] = (b24 >> 8) & 0xff;
		leds->altPixelBuffer[num+8] = (b24) & 0xff;
	}
}

/**
  * @brief Sends the pixel data via SPI using the DMA mode
  * @param none
  * @retval none
  */
ArgbErrorState showSpi(PixelDriver* leds)
{
	if(HAL_SPI_Transmit_DMA(leds->hspi, leds->altPixelBuffer, PIXEL_BUFFER_SIZE_SPI(leds->numPixels)) != HAL_OK)
	{
		return ArgbHalError;
	}
	return ArgbOk;
}

/**
  * @brief Bit-bangs pixelBuffer according to WS2812B specifications
  * @param none
  * @retval none
  */
void show(PixelDriver* leds)
{
	uint32_t frame = 0;
	__disable_irq();
	for(int j=0; j<leds->numPixels; j++)
	{
		frame = leds->pixelBuffer[j];
		for(int i=0; i<24; i++)
		{
			if((frame >> i) & 1)
			{
				(DATA_PORT)->ODR |= (DATA_PIN);
				asm(	"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP");
				(DATA_PORT)->ODR &= ~(DATA_PIN);
				asm(	"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP");
			}
			else
			{
				(DATA_PORT)->ODR |= (DATA_PIN);
				asm(	"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP");
				(DATA_PORT)->ODR &= ~(DATA_PIN);
				asm(	"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP"	"\n\t"
						"NOP");
			}
		}
	}
	__enable_irq();
	//pull line low to ensure reset pulse is sent
	//TODO: use timer instead of blocking
	(DATA_PORT)->ODR &= ~(DATA_PIN);
	for(int i=0; i<100; i++)
	{
		asm("NOP");
	}
#ifdef __cplusplus
}
#endif
}


/**
 * Creates an 8-bit gamma correction table for SK6812-mini LEDs
 * 
 * @param gamma The gamma value to use (typically 2.8 for LEDs)
 * @param brightness Overall brightness scaling (0.0 to 1.0)
 * @param table Output array of 256 bytes to store the gamma table
 */
void create_sk6812_gamma_table(float gamma, float brightness, uint8_t table[256])
{
    // Constrain brightness to valid range
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    
    // SK6812-mini LEDs often benefit from a slightly different gamma at low levels
    const float low_gamma = gamma * 0.7f; // Adjust gamma for low values
    
    for (int i = 0; i < 256; i++) {
        float normalized = (float)i / 255.0f;
        float corrected;
        
        // Apply different gamma for low values to get better low-level control
        if (normalized < 0.1f) {
            corrected = powf(normalized / 0.1f, low_gamma) * 0.1f;
        } else {
            corrected = powf(normalized, gamma);
        }
        
        // Apply brightness scaling
        corrected *= brightness;
        
        // Clamp and convert to 8-bit
        if (corrected > 1.0f) corrected = 1.0f;
        table[i] = (uint8_t)(corrected * 255.0f + 0.5f);
    }
}
