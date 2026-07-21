#ifndef HW_LED_H_
#define HW_LED_H_

int hw_led_start(void);

/** Stop LED blink and drive both LEDs inactive (e.g. before sys_poweroff). */
void hw_led_prepare_poweroff(void);

#endif /* HW_LED_H_ */
