#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

esp_err_t sleepMonitor_init(void);

esp_err_t sleepMonitor_querySN(uint8_t channel);
esp_err_t sleepMonitor_queryHrRr(uint8_t channel);
esp_err_t sleepMonitor_querySleepState(uint8_t channel);
esp_err_t sleepMonitor_queryFatigue(uint8_t channel);
esp_err_t sleepMonitor_queryBreathHold(uint8_t channel);
esp_err_t sleepMonitor_queryStress(uint8_t channel);
esp_err_t sleepMonitor_queryAdData(uint8_t channel);
esp_err_t sleepMonitor_queryAll(uint8_t channel);

esp_err_t sleepMonitor_autoReportHrRr(uint8_t channel, bool enable);
esp_err_t sleepMonitor_autoReportVital(uint8_t channel, bool enable);
esp_err_t sleepMonitor_autoReportAd(uint8_t channel, bool enable);

void sleepMonitor_setPollingMode(void);
void sleepMonitor_setAutoReportMode(void);
void sleepMonitor_pollRx(void);
