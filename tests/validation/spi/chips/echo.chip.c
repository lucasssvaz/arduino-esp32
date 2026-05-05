// Wokwi Custom SPI Echo Chip
//
// Echoes back in the CURRENT CS transaction exactly the bytes that were
// received in the PREVIOUS CS transaction, byte-for-byte with no transformation.
// On the very first transaction the chip sends all-zero bytes.
//
// This design makes it easy to test the full Arduino SPI API:
//   1. Prime transaction  – master sends payload; master receives zeros (ignored)
//   2. Echo  transaction  – master sends zeros; master receives its own payload back
//
// Implementation note:
//   A single spi_start(MAX_BUF) covers the entire CS transaction.  Wokwi drives
//   MISO byte-by-byte from xfer_buf[] while storing each received MOSI byte back
//   into xfer_buf[].  chip_spi_done is only ever called from within spi_stop()
//   (the CS HIGH handler), so the full received payload is always available before
//   the next CS LOW copies it into xfer_buf for echoing.  This eliminates the
//   race between per-byte callbacks and CS pin-change events that exists in the
//   byte-by-byte spi_start(count=1) pattern.
//
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Must be >= the largest single SPI transaction the chip will need to handle.
// 512 bytes provides ample headroom for current and future tests.
#define MAX_BUF 512

typedef struct {
  pin_t    cs_pin;
  uint32_t spi;
  uint8_t  xfer_buf[MAX_BUF];  // active SPI buffer: pre-loaded with echo data before
                                // spi_start; Wokwi overwrites it byte-by-byte with the
                                // received MOSI data during the exchange
  uint8_t  echo_buf[MAX_BUF];  // received bytes from the last completed CS transaction;
                                // copied into xfer_buf at the start of each new transaction
  uint32_t echo_len;            // number of valid bytes in echo_buf
} chip_state_t;

static void chip_pin_change(void *user_data, pin_t pin, uint32_t value);
static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count);

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  memset(chip->xfer_buf, 0, MAX_BUF);
  memset(chip->echo_buf, 0, MAX_BUF);
  chip->echo_len = 0;

  chip->cs_pin = pin_init("CS", INPUT_PULLUP);

  const pin_watch_config_t watch_config = {
    .edge      = BOTH,
    .pin_change = chip_pin_change,
    .user_data  = chip,
  };
  pin_watch(chip->cs_pin, &watch_config);

  const spi_config_t spi_config = {
    .sck      = pin_init("SCK",  INPUT),
    .miso     = pin_init("MISO", INPUT),
    .mosi     = pin_init("MOSI", INPUT),
    .done     = chip_spi_done,
    .user_data = chip,
  };
  chip->spi = spi_init(&spi_config);

  printf("SPI Echo Chip initialized\n");
}

static void chip_pin_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (pin != chip->cs_pin) {
    return;
  }

  if (value == LOW) {
    // CS asserted: pre-load xfer_buf with the bytes received in the previous
    // transaction (echo_buf), then start a MAX_BUF-byte SPI session.
    // Wokwi will send xfer_buf[i] as MISO for byte i and store the received
    // MOSI byte back into xfer_buf[i].
    memcpy(chip->xfer_buf, chip->echo_buf, chip->echo_len);
    if (chip->echo_len < MAX_BUF) {
      memset(chip->xfer_buf + chip->echo_len, 0, MAX_BUF - chip->echo_len);
    }
    spi_start(chip->spi, chip->xfer_buf, MAX_BUF);
  } else {
    // CS deasserted: stop the SPI engine.
    // Wokwi calls chip_spi_done(count = bytes fully exchanged) from within
    // spi_stop(), where we snapshot the received data into echo_buf.
    spi_stop(chip->spi);
  }
}

static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (!count) {
    // CS deasserted before any SCK edges – nothing received, nothing to echo.
    return;
  }

  // buffer[0..count-1] now holds the MOSI bytes received from the master.
  // Save them so the next CS transaction can echo them back as MISO.
  memcpy(chip->echo_buf, buffer, count);
  chip->echo_len = count;
}
