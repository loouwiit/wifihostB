#include "driver/gpio.h"
#include "driver/ledc.h"

void initPWM();
void startPWM(ledc_channel_t channel, gpio_num_t gpio);
void setPWMDuty(ledc_channel_t channel, uint32_t duty, uint32_t time);
long getPWMDuty(ledc_channel_t channel);
