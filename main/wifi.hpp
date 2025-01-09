#include <esp_wifi_types_generic.h>

void wifiInit();
void wifiDeinit();

bool wifiIsStarted();
void wifiStart();
void wifiStop();

bool wifiStationIsStarted();
void wifiStationStart();
void wifiStationStop();
uint16_t wifiStationScan(wifi_ap_record_t* apInfo, uint16_t maxCount, char* ssid = nullptr);

bool wifiIsWantConnect();
bool wifiIsConnect();
void wifiConnect(const char* ssid, const char* password, unsigned char retryTime = 3);
void wifiDisconnect();

bool wifiApIsStarted();
void wifiApStart();
void wifiApSet(const char* ssid, const char* password, wifi_auth_mode_t authMode = wifi_auth_mode_t::WIFI_AUTH_WPA2_WPA3_PSK);
void wifiApStop();

void wifiNatSetAutoStart(bool flag = true);
bool wifiNatIsAutoStart();

bool wifiNatIsStarted();
void wifiNatStart();
void wifiNatStop();
