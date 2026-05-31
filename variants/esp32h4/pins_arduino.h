#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#include "soc/soc_caps.h"

// GPIO6-12 are used by SPI0/1 flash and PSRAM, GPIO13/14 by USB Serial/JTAG (GPIO14 is BOOT),
// GPIO21/22 by USB-OTG and GPIO17/36/37 are strapping pins.

#define PIN_RGB_LED 27
// BUILTIN_LED can be used in new Arduino API digitalWrite() like in Blink.ino
static const uint8_t LED_BUILTIN = SOC_GPIO_PIN_COUNT + PIN_RGB_LED;
#define BUILTIN_LED LED_BUILTIN  // backward compatibility
#define LED_BUILTIN LED_BUILTIN  // allow testing #ifdef LED_BUILTIN
// RGB_BUILTIN and RGB_BRIGHTNESS can be used in new Arduino API rgbLedWrite()
#define RGB_BUILTIN    LED_BUILTIN
#define RGB_BRIGHTNESS 64

static const uint8_t TX = 24;
static const uint8_t RX = 23;

static const uint8_t SDA = 25;
static const uint8_t SCL = 26;

static const uint8_t SS = 20;
static const uint8_t MOSI = 18;
static const uint8_t MISO = 15;
static const uint8_t SCK = 16;

// USB-OTG pins (FS)
static const uint8_t USB_DM = 21;
static const uint8_t USB_DP = 22;

static const uint8_t A0 = 28;
static const uint8_t A1 = 29;
static const uint8_t A2 = 30;
static const uint8_t A3 = 31;
static const uint8_t A4 = 32;

static const uint8_t T0 = 0;
static const uint8_t T1 = 1;
static const uint8_t T2 = 2;
static const uint8_t T3 = 3;
static const uint8_t T4 = 29;
static const uint8_t T5 = 30;
static const uint8_t T6 = 31;
static const uint8_t T7 = 32;
static const uint8_t T8 = 33;
static const uint8_t T9 = 34;
static const uint8_t T10 = 35;
static const uint8_t T11 = 36;
static const uint8_t T12 = 37;
static const uint8_t T13 = 38;
static const uint8_t T14 = 39;

#endif /* Pins_Arduino_h */
