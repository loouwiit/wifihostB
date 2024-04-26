#include <driver/temperature_sensor.h>
#include "tempture.hpp"

static temperature_sensor_config_t temp_sensor;
static temperature_sensor_handle_t temp_handle = NULL;
static float temperature;
static bool tempratureEnabled = false;

void initTemperature()
{
	temp_sensor.range_min = -10;
	temp_sensor.range_max = 80;
	ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor, &temp_handle));
}

void startTemperature()
{
	ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
	tempratureEnabled = true;
}

void stopTemperature()
{
	tempratureEnabled = false;
	ESP_ERROR_CHECK(temperature_sensor_disable(temp_handle));
}

float getTemperature()
{
	if (!tempratureEnabled) return FLT_MAX;
	auto ret = temperature_sensor_get_celsius(temp_handle, &temperature);
	if (ret == ESP_OK)
		return temperature;
	else
	{
		printf("temperature: get temperature error: %d\n\ttemperature is %f\n", ret, temperature);
		ESP_ERROR_CHECK(ret);
		return FLT_MAX;
	}
}
