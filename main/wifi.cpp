#include <cstring>

#include <nvs_flash.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_random.h>

#include "freertos/event_groups.h"

#include "wifi.hpp"
#include "wifi.inl" //wifi password and ssid

#define EXAMPLE_ESP_MAXIMUM_RETRY 3

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const char* TAG = "wifi station";

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool isReconnectEnable = false;
static bool isWifiConnected = false;

static esp_event_handler_instance_t instance_any_id;
static esp_event_handler_instance_t instance_got_ip;

void event_handler(void* arg, esp_event_base_t event_base,
	int32_t event_id, void* event_data)
{
	printf("event_handler: event id = %ld\n", event_id);
	if (event_base == WIFI_EVENT)
	{
		if (event_id == WIFI_EVENT_STA_DISCONNECTED)
		{
			if (isReconnectEnable)
			{
				// 若想要连接再重联
				if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
				{
					esp_wifi_connect();
					s_retry_num++;
					ESP_LOGI(TAG, "retry to connect to the AP");
				}
				else
				{
					xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
				}
				ESP_LOGI(TAG, "connect to the AP fail");
			}
		}
	}
	else if (event_base == IP_EVENT)
	{
		if (event_id == IP_EVENT_STA_GOT_IP)
		{
			ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
			ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
			xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
		}
	}
}

void initNVS()
{
	printf("NVS init\n");
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
	printf("NVS inited\n");
}

void wifiInit()
{
	initNVS();
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	// 注册消息
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL, &instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	// ESP_ERROR_CHECK(esp_wifi_start());
}

void wifiDeinit()
{
	esp_wifi_deinit();
}

void wifiInitSta()
{
	wifi_config_t wifi_sta_config = {};

	sprintf((char*)wifi_sta_config.sta.ssid, "%s", WIFISSID);
	sprintf((char*)wifi_sta_config.sta.password, "%s", WIFIPASSWORD);
	wifi_sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

	printf("wifi inited.\n");
}

// void wifiInitAp()
// {
// 	wifi_config_t wifi_ap_config = {};
// 	char ssid[32] = "ESP32S3";
// 	char password[64] = "12345678";

// 	sprintf((char*)wifi_ap_config.ap.ssid, "%s", ssid);
// 	wifi_ap_config.ap.ssid_len = strlen(ssid);
// 	sprintf((char*)wifi_ap_config.ap.password, "%s", password);
// 	wifi_ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
// 	wifi_ap_config.ap.max_connection = 10;

// 	// ap
// 	esp_netif_create_default_wifi_ap();
// 	// esp_netif_t* netif = esp_netif_create_default_wifi_ap();
// 	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

// 	printf("AP inited\n");
// }

void wifiStart()
{
	ESP_ERROR_CHECK(esp_wifi_start());
	isWifiConnected = false;
	printf("wifi started\n");
}

void wifiStop()
{
	ESP_ERROR_CHECK(esp_wifi_stop());
	isWifiConnected = false;
	printf("wifi stopped\n");
}

void wifiConnect()
{
	isReconnectEnable = true;
	s_retry_num = 0;
	ESP_ERROR_CHECK(esp_wifi_connect());

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = 0;
	unsigned int waiting_tick = 0;
	do
	{
		printf("[%d]:waiting for bits.\n", waiting_tick);
		waiting_tick++;

		bits = xEventGroupWaitBits(s_wifi_event_group,
			WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
			pdFALSE,
			pdFALSE,
			1000 / portTICK_PERIOD_MS);
	} while (bits == 0);
	xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened. */
	if (bits & WIFI_CONNECTED_BIT)
	{
		isWifiConnected = true;
		ESP_LOGI(TAG, "connected");
	}
	else if (bits & WIFI_FAIL_BIT)
	{
		ESP_LOGI(TAG, "Failed to connect");
	}
	else
	{
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}
}

void wifiDisconnect()
{
	isReconnectEnable = false;
	isWifiConnected = false;
	esp_wifi_disconnect();
	printf("wifi disconnected\n");
}

bool wifiIsConnect()
{
	// 理论上 aid == 0 意味着未连接
	// 但是返回一直有值，于是自己写一个bool得了
	// uint16_t aid;
	// esp_wifi_sta_get_aid(&aid);
	// return aid != 0;
	return isWifiConnected;
}
