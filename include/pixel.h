/*
 * ws2812_driver.h
 *
 *  Created on: 19Apr.,2019
 *      Author: samspencer
 */

#ifndef PIXEL_H_
#define PIXEL_H_

#include <stdint.h>
#include "spi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_PIXELS 8

typedef enum
{
	ArgbParamError,
	ArgbMemError,
	ArgbHalError,
	ArgbOk
} ArgbErrorState;

typedef enum
{
	LedGpio,
	LedSpi,
	LedPwm
} LedProtocol;

typedef struct
{
	LedProtocol protocol;		// GPIO (bit-bang), SPI (DMA), PWM (DMA)
	uint8_t colourMode;			// 1 = RGB, 2 = BRG, 3 = GBR
	uint32_t* pixelBuffer;		// 24-bit colour buffer (one uint32_t per pixel)
	uint16_t numPixels;			// Total number of LEDs
	uint8_t brightness;			// Global brightness scaling
	TIM_HandleTypeDef* htim;	// STM32 HAL timer handle
	uint16_t timChannel;			// PWM only
	SPI_HandleTypeDef* hspi;	// STM32 HAL SPI handle
	uint8_t* altPixelBuffer;	// Used for SPI bit packets or PWM data
	uint8_t ready;					// Indicates if the previous transmission is complete. Set init value to 0 or undefined
} PixelDriver;

#define ORDER_RGB 1
#define ORDER_BRG 2
#define ORDER_GBR 3

// 24-bit colour macros (RGB)
#define WHITE 			0xffffff
#define RED 				0xad323C
#define GREEN 			0x00ff00
#define DULL_GREEN	0x4a7023
#define BLUE 				0x1f529e
#define AQUA 				0x00ffff
#define YELLOW 			0xefd856
#define PURPLE 			0xff00ff
#define CORAL 			0xf08080
#define OLIVE 			0x9abd32
#define PINK 				0xff1493
#define PEACH				0xed823e
#define ORANGE			0xef7f39
#define SKY 				0x9cc9e8
#define SEA_FOAM		0x6be58c
#define OFF 				0x000000

#define SPI_BYTE_MULTIPLIER	9
#define NUM_SPI_RESET_BYTES 2

// Used to define the altPixelBuffer size for SPI
#define PIXEL_BUFFER_SIZE_SPI(num) (num * SPI_BYTE_MULTIPLIER + NUM_SPI_RESET_BYTES)

// SHARED FUNCTIONS 
void pixel_Init(PixelDriver* leds);
void pixel_SetPixel(PixelDriver* leds, uint16_t index, uint32_t colour);
ArgbErrorState pixel_Show(PixelDriver* leds);
uint32_t pixel_ScaleColour(uint32_t colour, uint8_t brightness);

#ifdef __cplusplus
}
#endif
#endif // PIXEL_H_
