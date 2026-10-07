// Flex OS Ultra · GUITION JC4880P443C_I_W (JC4880P443 V1.0) · ESP32-P4
//
// UNICO sitio con pines, geometria y temporizacion de la placa. Los datos
// proceden de la version Arduino que funciona hoy en la placa
// (FlexOS_Ultra/FlexOS_Ultra_HAL.h y FlexOS_Ultra_Types.h); son datos del
// fabricante, no del motor grafico.
#pragma once

#include <stdint.h>

// ---- Pantalla: 480x800 vertical nativa (regla absoluta del proyecto) -------
#define FLEX_LCD_H_RES              480
#define FLEX_LCD_V_RES              800
#define FLEX_LCD_BITS_PER_PIXEL     16      // RGB565

// MIPI-DSI hacia el ST7701
#define FLEX_DSI_LANES              2
#define FLEX_DSI_LANE_MBPS          500
#define FLEX_DSI_PHY_LDO_CHAN       3       // LDO interno del P4 que alimenta el PHY
#define FLEX_DSI_PHY_LDO_MV         2500

// Temporizacion DPI (34 MHz; 576 x 976 ciclos por cuadro = ~60,5 Hz)
#define FLEX_DPI_CLK_MHZ            34
#define FLEX_DPI_HSYNC_PW           12
#define FLEX_DPI_HSYNC_BP           42
#define FLEX_DPI_HSYNC_FP           42
#define FLEX_DPI_VSYNC_PW           2
#define FLEX_DPI_VSYNC_BP           8
#define FLEX_DPI_VSYNC_FP           166

#define FLEX_PIN_LCD_RST            5
#define FLEX_PIN_LCD_BL             23
#define FLEX_BL_PWM_HZ              20000

// ---- Bus I2C compartido: GT911 (0x5D/0x14), ES8311 (0x18), BNO085 (0x4A/0x4B)
#define FLEX_PIN_I2C_SDA            7
#define FLEX_PIN_I2C_SCL            8
#define FLEX_I2C_HZ                 400000

// ---- Tactil GT911 -----------------------------------------------------------
#define FLEX_PIN_TP_RST             3
// INT del GT911: cableado DESCONOCIDO (pregunta abierta 6 del plan). Sin INT
// el tactil se sondea; -1 = sin pin.
#define FLEX_PIN_TP_INT             (-1)
// Si un lote sale espejado o cruzado (mismos interruptores que la version Arduino)
#define FLEX_TP_SWAP_XY             0
#define FLEX_TP_FLIP_X              0
#define FLEX_TP_FLIP_Y              0
