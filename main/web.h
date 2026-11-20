#pragma once
#include "esp_err.h"
#include "esp_eth_driver.h"
esp_err_t web_start(esp_eth_handle_t ethernet);
void web_note_airspy(void);
