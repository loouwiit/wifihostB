#include <pwm.hpp>

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define LEDC_LS_TIMER          LEDC_TIMER_1
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE

constexpr long LEDC_Freq = 50000;

ledc_channel_config_t ledc_channel[ledc_channel_t::LEDC_CHANNEL_MAX]{ {} }; //好像没啥用，之后再优化吧
SemaphoreHandle_t counting_sem[ledc_channel_t::LEDC_CHANNEL_MAX] = { 0 };

static IRAM_ATTR bool cb_ledc_fade_end_event(const ledc_cb_param_t* param, void* user_arg)
{
	portBASE_TYPE taskAwoken = pdFALSE;

	if (param->event == LEDC_FADE_END_EVT) {
		SemaphoreHandle_t counting_sem = (SemaphoreHandle_t)user_arg;
		xSemaphoreGiveFromISR(counting_sem, &taskAwoken);
	}

	return (taskAwoken == pdTRUE);
}

void initPWM()
{
	printf("init PWM\n");
	/*
	 * Prepare and set configuration of timers
	 * that will be used by LED Controller
	 */
	ledc_timer_config_t ledc_timer{};
	ledc_timer.duty_resolution = LEDC_TIMER_10_BIT; // resolution of PWM duty
	ledc_timer.freq_hz = LEDC_Freq;                 // frequency of PWM signal
	ledc_timer.speed_mode = LEDC_LS_MODE;           // timer mode
	ledc_timer.timer_num = LEDC_LS_TIMER;           // timer index
	ledc_timer.clk_cfg = LEDC_AUTO_CLK;             // Auto select the source clock
	// Set configuration of timer0 for high speed channels
	ledc_timer_config(&ledc_timer);

	// Initialize fade service.
	ledc_fade_func_install(0);
}

void startPWM(ledc_channel_t channel, gpio_num_t gpio)
{
	printf("LEDC %d set to GPIO %d\n", channel, gpio);
	/*
	 * Prepare individual configuration
	 * for each channel of LED Controller
	 * by selecting:
	 * - controller's channel number
	 * - output duty cycle, set initially to 0
	 * - GPIO number where LED is connected to
	 * - speed mode, either high or low
	 * - timer servicing selected channel
	 *   Note: if different channels use one timer,
	 *         then frequency and bit_num of these channels
	 *         will be the same
	 */

	ledc_channel[channel].channel = channel;
	ledc_channel[channel].duty = 0;
	ledc_channel[channel].gpio_num = gpio;
	ledc_channel[channel].speed_mode = LEDC_LS_MODE;
	ledc_channel[channel].hpoint = 0;
	ledc_channel[channel].timer_sel = LEDC_LS_TIMER;
	ledc_channel[channel].flags.output_invert = 0;

	// Set LED Controller with previously prepared configuration
	ledc_channel_config(&ledc_channel[channel]);

	// Initialize fade service.
	ledc_cbs_t callbacks = {
		.fade_cb = cb_ledc_fade_end_event
	};
	counting_sem[channel] = xSemaphoreCreateCounting(gpio, 0);

	ledc_cb_register(ledc_channel[channel].speed_mode, ledc_channel[channel].channel, &callbacks, (void*)counting_sem[channel]);
}

void setPWMDuty(ledc_channel_t channel, uint32_t duty, uint32_t time)
{
	printf("LEDC %d (GPIO %d) fade up to duty %ld\n", channel, ledc_channel[channel].gpio_num, duty);
	ledc_set_fade_with_time(ledc_channel[channel].speed_mode,
		ledc_channel[channel].channel, duty, time);
	ledc_fade_start(ledc_channel[channel].speed_mode,
		ledc_channel[channel].channel, LEDC_FADE_NO_WAIT);

	xSemaphoreTake(counting_sem[channel], portMAX_DELAY);
}

long getPWMDuty(ledc_channel_t channel)
{
	return ledc_get_duty(ledc_channel[channel].speed_mode, channel);
}
