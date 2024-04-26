#include "driver/gpio.h"
#include "driver/ledc.h"

void startPWM(gpio_num_t gpio);
void setPWMDuty(uint32_t duty, uint32_t time);

long getPWMDuty();
