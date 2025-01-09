#include <stdio.h>
#include <esp_system.h>
#include <esp_task.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/portmacro.h>
#include <esp_event.h>
#include "tempture.hpp"
#include "pwm.hpp"
#include "nvs.hpp"
#include "wifi.hpp"
#include "wifi.inl" //WIFISSID & WIFIPASSWORD & APSSID & APPASSWORD
#include "fat.hpp"
#include "mem.hpp"
#include "server/head/server.hpp"

extern bool serverRunning;

QueueHandle_t gpio_evt_queue = NULL;

extern "C" { void app_main(); }
void setIo0();
void ioInterapt(void* arg);
void ioPressed(void* arg);

void app_main()
{
	initPWM();
	startPWM(ledc_channel_t::LEDC_CHANNEL_0, gpio_num_t::GPIO_NUM_1);
	startPWM(ledc_channel_t::LEDC_CHANNEL_1, gpio_num_t::GPIO_NUM_44);
	setPWMDuty(ledc_channel_t::LEDC_CHANNEL_0, 0, 1);
	setPWMDuty(ledc_channel_t::LEDC_CHANNEL_1, 128, 500);


	setIo0();
	mountFlash();
	mountMem();
	tree(PerfixRoot); //[debug]

	nvsInit();
	wifiInit();
	wifiNatSetAutoStart(true);
	wifiStart();

	{
		//Station模式连接wifi
		wifiStationStart();
		wifiConnect(WIFISSID, WIFIPASSWORD);

		unsigned count = 0;
		do
		{
			printf("[%u]waiting for connect\n", count++);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		} while (wifiIsWantConnect() && !wifiIsConnect()); //想连接但还没连上

		if (!wifiIsConnect())
		{
			//连接失败
			wifiStationStop();

			//启动AP
			wifiApStart();
			wifiApSet(APSSID, APPASSWORD);
		}
	}

	{
		serverRunning = true;
		startServer();
	}

	initTemperature();
	startTemperature();

	printf("started\n");

	setPWMDuty(ledc_channel_t::LEDC_CHANNEL_1, 1024, 500);

#if false
	while (1)
	{
		printf("tempture: %f\n", getTemperature());
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
#endif

	return;
}

void setIo0()
{
	// gpio_install_isr_service(0);
	// gpio_set_direction(0, GPIO_MODE_INPUT);
	// gpio_set_intr_type(0, GPIO_INTR_POSEDGE);
	// gpio_set_pull_mode(0 ,GPIO_PULLUP_ENABLE);
	// gpio_isr_handler_add(0, boot0Pressed, NULL);
	// gpio_intr_enable(0);

	gpio_config_t io_conf = {}; //zero-initialize the config structure.
	io_conf.intr_type = GPIO_INTR_NEGEDGE;
	io_conf.pin_bit_mask = 1 << 0; //bit mask of the pins, use GPIO0 here
	io_conf.mode = GPIO_MODE_INPUT; //set as input mode
	io_conf.pull_up_en = (gpio_pullup_t)1; //enable pull-up mode
	gpio_config(&io_conf);

	gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
	xTaskCreate(ioPressed, "ioPressed", 3072, NULL, 10, NULL);

	gpio_install_isr_service(0);
	//hook isr handler for specific gpio pin
	gpio_isr_handler_add((gpio_num_t)0, ioInterapt, (void*)0);
}

void ioInterapt(void* arg)
{
	uint32_t gpio_num = (uint32_t)arg;
	xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}


void ioPressed(void* arg)
{
	uint32_t io_num;
	while (1)
	{
		if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
		{
			printf("GPIO[%lu] intr, val: %d\n", io_num, gpio_get_level((gpio_num_t)io_num));

			if (!wifiIsConnect())
			{
				// 没有wifi连接->启动Ap
				stopTemperature();
				if (!wifiIsStarted()) wifiStart();
				if (wifiStationIsStarted()) wifiStationStop();
				if (!wifiApIsStarted())
				{
					wifiApStart();
					wifiApSet(APSSID, APPASSWORD);
				}
				startTemperature();
			}

			if (!serverRunning)
			{
				serverRunning = true;
				startServer();
			}
		}
	}
}
