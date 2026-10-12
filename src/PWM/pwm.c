#include "pwm.h"
#include <zephyr/drivers/pwm.h>
#include <zephyr/dt-bindings/pwm/pwm.h>

static const struct device *const pwm_dev = DEVICE_DT_GET(DT_NODELABEL(pwm0));

#define PWM_PERIOD_NS	42U
#define PWM_PULSE_NS	(PWM_PERIOD_NS / 2U)
#define PWM_GPT_CHANNEL 0

int pwm_service_init(void)
{
	int ret;

	if (!device_is_ready(pwm_dev)) {
		printk("pwm_dev not ready\n");
		return -ENODEV;
	}

	ret = pwm_set(pwm_dev, PWM_GPT_CHANNEL, PWM_PERIOD_NS, PWM_PULSE_NS,
		      PWM_POLARITY_NORMAL);
	if (ret != 0) {
		printk("pwm_set: %d\n", ret);
		return ret;
	}

	printk("PWM started\n");

	return 0;
}