#ifndef WEB_UI_H
#define WEB_UI_H

#include "core/config.h"

void initWifiAP();
bool startApMode(const AppConfig &config);
bool startStaMode(const AppConfig &config, uint32_t timeoutMs = 15000);
void setupWifiFromConfig(const AppConfig &config);
void initWebUi();
void webUiLoop();

#endif // WEB_UI_H
