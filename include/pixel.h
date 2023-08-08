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

#define ORDER_RGB 1
#define ORDER_BRG 2
#define ORDER_GBR 3

/* 24-bit colour macros (RGB) */
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

#define PIXEL_BUFFER_SIZE_SPI(num) (num * SPI_BYTE_MULTIPLIER + NUM_SPI_RESET_BYTES)

/* BIT-BANGING FUNCTIONS */
void ws2812_init(uint8_t colourMode, uint16_t num, uint8_t* buf);
void ws2812_setPixel(uint16_t num, uint32_t colour);
void ws2812_show();

/* SPI-DMA FUNCTIONS */
void ws2812_initSpi(uint8_t mode, uint16_t num, SPI_HandleTypeDef *spiHandle, uint8_t* buf);
void ws2812_clearSpi();
void ws2812_setPixelSpi(uint16_t num, uint32_t colour);
ArgbErrorState ws2812_showSpi();

/* SHARED FUNCTIONS */
void ws2812_setBrightness(uint8_t newBrightness);
uint8_t ws2812_getBrightness();
uint32_t ws2812_scaleColour(uint32_t colour, uint8_t brightness);

#ifdef __cplusplus
}
#endif
#endif /* PIXEL_H_ */
