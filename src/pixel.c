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

#define HIGH_BIT 0b110
#define LOW_BIT 0b100

#define DATA_PIN GPIO_PIN_15
#define DATA_PORT GPIOB

#define SPI_BYTE_MULTIPLIER	9
#define NUM_SPI_RESET_BYTES 2

/*Adafruit's WS2812 Gamma Reduction Table*/
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

uint8_t colourOrder;
uint16_t numPixels;			// Number of physical LEDs (pixels) used
uint16_t spiBufferSize;		// Number of elements in the SPI buffer for numPixels
SPI_HandleTypeDef *hspi;	// Hardware HAL spi handle
uint8_t brightness = 200;	// Note that 56 is the lowest brightness that maintains accurate colour

/**
 * Holds the SPI format data to be sent. Each pixel bit (1 or 0 pulse) takes up 3 SPI bits
 * So, a single colour channel (R, G, or B) for a pixel takes up 24 storage bits
 * Thus, the pixelBuffer_SPI needs to store 3 24-bit chunks of data for each pixel
*/
uint8_t pixelBufferSpi[NUM_PIXELS * SPI_BYTE_MULTIPLIER + NUM_SPI_RESET_BYTES];

// Pixel buffer if non-spi implementation is used
uint32_t pixelBuffer[NUM_PIXELS * SPI_BYTE_MULTIPLIER + NUM_SPI_RESET_BYTES];

/**
  * @brief Initialise default starts on startup for LEDs
  * @param colourMode	sets the order for r,g,b components:
  * 							1 = RGB
  * 							2 = BRG
  * 							3 = GBR
  * @retval none
  */
void ws2812_init(uint8_t mode, uint16_t num)
{
	colourOrder = mode;
	numPixels = num;
	for(int i=0; i<numPixels; i++)
	{
		pixelBuffer[i] = OFF;
	}
	ws2812_show();
}

/**
  * @brief Initialise default starts on startup for LEDs using SPI mode
  * @param colourMode	sets the order for r,g,b components:
  * 							1 = RGB
  * 							2 = BRG
  * 							3 = GBR
  * @retval none
  */
void ws2812_initSpi(uint8_t mode, uint16_t num, SPI_HandleTypeDef *spiHandle)
{
	// Assign non-dynamic parameters
	colourOrder = mode;
	numPixels = num;
	spiBufferSize = numPixels * SPI_BYTE_MULTIPLIER + NUM_SPI_RESET_BYTES;
	hspi = spiHandle;
	if(pixelBufferSpi == NULL)
	{
		return ArgbMemError;
	}
	uint8_t resetCounter = (spiBufferSize - NUM_SPI_RESET_BYTES);
	for(uint8_t i=resetCounter; i<spiBufferSize; i++)
	{
		pixelBufferSpi[i] = 0x00;
	}
	ws2812_clearSpi();
	ws2812_showSpi();

}

void ws2812_setBrightness(uint8_t newBrightness)
{
	brightness = newBrightness;
	return;
}

uint8_t ws2812_getBrightness()
{
	return brightness;
}

uint32_t ws2812_scaleColour(uint32_t colour, uint8_t brightness)
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
  * @param none
  * @retval none
  */
void ws2812_clearSpi()
{
	for(int i=0; i<numPixels; i++)
	{
		ws2812_setPixelSpi(i, OFF);
	}
}

/**
  * @brief Checks the colour mode and assigns pixel number (num) a 24-bit colour value (colour)
  * @param num		pixel number to set
  * @param colour	24-bit colour to assign to pixel
  * @retval none
  */
void ws2812_setPixel(uint16_t num, uint32_t colour)
{
	uint32_t outputColour = 0x00000000;
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	if(num < numPixels)
	{
		if(colourOrder == ORDER_RGB)
		{
			r = (colour >> 16) & 0xff;
			g = (colour >> 8) & 0xff;
			b = (colour) & 0xff;
		}
		else if(colourOrder == ORDER_GBR)
		{
			r = (colour) & 0xff;
			g = (colour >> 16) & 0xff;
			b = (colour >> 8) & 0xff;
		}
		outputColour = g | (r<<8) | (b<<16);
		pixelBuffer[num] = outputColour;
	}
}

/**
  * @brief Checks the colour mode and assigns pixel number (num) a 24-bit colour value (colour)
  * @param num		pixel number to set
  * @param colour	24-bit colour to assign to pixel
  * @retval none
  */
void ws2812_setPixelSpi(uint16_t pixel, uint32_t colour)
{
	uint16_t num = pixel*9;
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
	unsigned long g24 = 0;
	unsigned long r24 = 0;
	unsigned long b24 = 0;

	if(pixel < numPixels)
	{
		//depending on the set order, extract each colour channel
		// Gamma correct is now post brightness scaling for better linearity
		if(colourOrder == ORDER_RGB)
		{
			r = (colour >> 16) & 0xff;
			g = (colour >> 8) & 0xff;
			b = (colour) & 0xff;
		}
		else if(colourOrder == ORDER_BRG)
		{
			r = (colour >> 8) & 0xff;
			g = (colour) & 0xff;
			b = (colour >> 16) & 0xff;
		}
		else if(colourOrder == ORDER_GBR)
		{
			r = gamma8[(colour) & 0xff];
			g = gamma8[(colour >> 16) & 0xff];
			b = gamma8[(colour >> 8) & 0xff];
		}

		// Scale colour channels according to set brightness
		// Check if a brightness has been set
		// Gamma correction now post brightnes
		if(brightness != 255)
		{
			float rChunk = (float)r / 255.00;
			float gChunk = (float)g / 255.00;
			float bChunk = (float)b / 255.00;

			r = rChunk * brightness;
			g = gChunk * brightness;
			b = bChunk * brightness;
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
		pixelBufferSpi[num] = (g24 >> 16) & 0xff;
		pixelBufferSpi[num+1] = (g24 >> 8) & 0xff;
		pixelBufferSpi[num+2] = (g24) & 0xff;

		pixelBufferSpi[num+3] = (r24 >> 16) & 0xff;
		pixelBufferSpi[num+4] = (r24 >> 8) & 0xff;
		pixelBufferSpi[num+5] = (r24) & 0xff;

		pixelBufferSpi[num+6] = (b24 >> 16) & 0xff;
		pixelBufferSpi[num+7] = (b24 >> 8) & 0xff;
		pixelBufferSpi[num+8] = (b24) & 0xff;
	}
}

/**
  * @brief Sends the pixel data via SPI using the DMA mode
  * @param none
  * @retval none
  */
ArgbErrorState ws2812_showSpi()
{
	if(HAL_SPI_Transmit_DMA(hspi, pixelBufferSpi, spiBufferSize) != HAL_OK)
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
void ws2812_show()
{
	uint32_t frame = 0;
	__disable_irq();
	for(int j=0; j<numPixels; j++)
	{
		frame = pixelBuffer[j];
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
}
