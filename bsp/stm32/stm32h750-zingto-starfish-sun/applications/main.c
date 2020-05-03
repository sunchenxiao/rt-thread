/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-03-05     whj4674672   first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* defined the LED0 pin: PE4 */
#define LED0_PIN    GET_PIN(E, 4)

int main(void)
{
	rt_pin_mode(LED0_PIN, PIN_MODE_OUTPUT);
    
	while(1)
	{
		rt_thread_mdelay(500);
		rt_pin_write(LED0_PIN,PIN_LOW);
		rt_thread_mdelay(500);
		rt_pin_write(LED0_PIN,PIN_HIGH);
	}
}
