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
#include "wifi.hpp"
#include "fat.hpp"
#include "server/head/server.hpp"

extern bool serverRunning;

QueueHandle_t gpio_evt_queue = NULL;

extern "C" { void app_main(); }
void setIo0();
void ioInterapt(void* arg);
void ioPressed(void* arg);

void app_main()
{
	setIo0();
	initTemperature();
	startTemperature();
	startPWM((gpio_num_t)5);
	mountFlash();

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	wifiInit();
	wifiInitSta();

	{
		stopTemperature();
		serverRunning = true;
		wifiStart();
		wifiConnect();
		startServer();
	}

	printf("started\n");

	setPWMDuty(1024, 500);
	setPWMDuty(0, 500);

	while (1)
	{
		printf("tempture: %f\n", getTemperature());
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}

	stopTemperature();
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
	xTaskCreate(ioPressed, "ioPressed", 1024, NULL, 10, NULL);

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

			if (!serverRunning)
			{
				stopTemperature();
				serverRunning = true;
				wifiStart();
				wifiConnect();
				startServer();
			}
		}
	}
}
