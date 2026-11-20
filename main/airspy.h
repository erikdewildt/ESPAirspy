#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#define AIRSPY_GAIN_MAX 9u

esp_err_t airspy_start(SemaphoreHandle_t ready);
bool airspy_connected(void);
bool airspy_iq_streaming(void);
/* Empty string when the receiver does not report a usable version. */
const char *airspy_firmware_version(void);
/* Copy the complete factory configuration read at receiver startup. */
bool airspy_configuration_backup(uint8_t out[256]);
const char *airspy_serial_number(void);
uint32_t airspy_sample_rate(void);
uint32_t airspy_frequency(void);
void airspy_request_gain(uint32_t gain);
uint32_t airspy_gain(void);
/* LNA / "postamp": 0 = off, 1 = +6 dB (SET_LNA, vendor request 13). */
void airspy_request_lna(uint32_t lna);
uint32_t airspy_lna(void);
/* AGC threshold: 0 = low, 1 = high (SET_AGC_THRESHOLD, request 11). Only
 * meaningful when receiver AGC is enabled; harmless otherwise. */
void airspy_request_agc_threshold(uint32_t high);
uint32_t airspy_agc_threshold(void);
uint32_t airspy_lo_frequency(void);
int32_t airspy_calibration_ppb(void);
bool airspy_calibration_valid(void);
uint32_t airspy_dropped_blocks(void);
uint32_t airspy_received_blocks(void);
uint32_t airspy_usb_completions(void);
uint32_t airspy_usb_empty(void);
void airspy_iq_buffer_stats(uint64_t *lost_samples, size_t *capacity, size_t *peak_estimate);
void airspy_input_level_counts(uint64_t *samples,uint64_t *near_rail,uint64_t *mixer_clipped);
void airspy_request_frequency(uint32_t hz);
void airspy_set_streaming(bool enabled);
size_t airspy_read(void *data, size_t length, TickType_t wait);

/* ---- Airspy HF+ firmware update (SAM-BA bootloader) support ---- */

/* Ask the receiver to reboot into its SAM-BA bootloader (vendor request 0x66).
 * The request is fire-and-forget: the receiver resets without completing the
 * status phase. */
esp_err_t airspy_reboot_to_samba(void);

/* True while the receiver's SAM-BA bootloader (VID:PID 03eb:6124) is open. */
bool airspy_samba_connected(void);

/* Blocking bulk OUT/IN to the SAM-BA bootloader's CDC data endpoints.
 * airspy_samba_write returns ESP_OK on success; airspy_samba_read returns the
 * number of bytes read (>= 0, 0 on timeout) or negative on error. */
esp_err_t airspy_samba_write(const uint8_t *data, size_t len);
int airspy_samba_read(uint8_t *data, size_t len, uint32_t timeout_ms);

/* Fully reset the connected receiver and restart the ESP32: the USB host root
 * port is powered off so the receiver's supply rail discharges, then the ESP32
 * reboots and re-powers the port during startup. */
void airspy_power_cycle_and_restart(void);

/* Briefly de-power and re-power the USB host root port, forcing the connected
 * receiver to fully reset and re-enumerate cleanly. */
void airspy_usb_power_cycle(void);

/* Enable/disable the runtime DSP experiment stages (DC blocker, IQ balance).
 * Only effective for stages compiled into this build; safe to call anytime. */
void airspy_set_dsp_options(bool dc_block, bool iq_balance);
