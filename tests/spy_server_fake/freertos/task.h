#pragma once
#include "FreeRTOS.h"
static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
static inline int xTaskCreatePinnedToCore(void (*task)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,int core)
{ (void)task;(void)name;(void)stack;(void)arg;(void)priority;(void)handle;(void)core;return pdPASS; }
