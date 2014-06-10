/*                    The Quest Operating System
 *  Copyright (C) 2005-2012  Richard West, Boston University
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* cy8c9540 driver */

#include "drivers/i2c/galileo_i2c.h"
#include "util/printf.h"

#define DEBUG_CYPRESS

#ifdef DEBUG_CYPRESS
#define DLOG(fmt,...) DLOG_PREFIX("CYPRESS",fmt,##__VA_ARGS__)
#else
#define DLOG(fmt,...) ;
#endif

#define TAR_ADDR   		0x20
#define NPORTS				6
#define NPWM          8
#define PWM_CLK				0x00	/* see resulting PWM_TCLK_NS */

/* Register offset  */
#define REG_INPUT_PORT0			0x00
#define REG_OUTPUT_PORT0		0x08
#define REG_INTR_STAT_PORT0		0x10
#define REG_PORT_SELECT			0x18
#define REG_INTR_MASK			0x19
#define REG_SELECT_PWM			0x1a
#define REG_PIN_DIR			0x1c
#define REG_DRIVE_PULLUP		0x1d
#define REG_PWM_SELECT			0x28
#define REG_PWM_CLK			0x29
#define REG_PWM_PERIOD			0x2a
#define REG_PWM_PULSE_W			0x2b
#define REG_ENABLE			0x2d
#define REG_DEVID_STAT			0x2e

#define BIT(nr)			(1UL << (nr))

#define GPIOF_DRIVE_PULLUP	(1 << 6)
#define GPIOF_DRIVE_PULLDOWN	(1 << 7)
#define GPIOF_DRIVE_STRONG	(1 << 8)
#define GPIOF_DRIVE_HIZ		(1 << 9)

/* Per-port GPIO offset */
static const u8 cy8c9540a_port_offs[] = {
	0,
	8,
	16,
	20,
	28,
	36,
};

struct cy8c9540a {
	/* cached output registers */
	u8 outreg_cache[NPORTS];
	/* cached IRQ mask */
	u8 irq_mask_cache[NPORTS];
	/* IRQ mask to be applied */
	u8 irq_mask[NPORTS];
  u32 addr;
};

static struct cy8c9540a dev = {
  .addr = TAR_ADDR,
};

static inline u8
cypress_get_port(unsigned gpio)
{
  u8 i = 0;
  for (i = 0; i < sizeof(cy8c9540a_port_offs) - 1; i++) {
    if (!(gpio / cy8c9540a_port_offs[i + 1]))
      break;
  }
  return i;
}

static inline u8 cypress_get_offs(unsigned gpio, u8 port)
{
	return gpio - cy8c9540a_port_offs[port];
}

int
cy8c9540a_gpio_get_value(unsigned gpio)
{
  s32 ret = 0;
  u8 port = 0, pin = 0, in_reg = 0;
  port = cypress_get_port(gpio);
  pin = cypress_get_offs(gpio, port);
  in_reg = REG_INPUT_PORT0 + port;

  ret = i2c_read_byte_data(in_reg);
  if (ret < 0) {
    DLOG("can't read input port%u\n", port);
  }

  return !!(ret & BIT(pin));
}

void
cy8c9540a_gpio_set_value(unsigned gpio, int val)
{
  s32 ret = 0;
  u8 port = 0, pin = 0, out_reg = 0;
  port = cypress_get_port(gpio);
  pin = cypress_get_offs(gpio, port);
  out_reg = REG_OUTPUT_PORT0 + port;

  if (val) {
    dev.outreg_cache[port] |= BIT(pin);
  } else {
    dev.outreg_cache[port] &= ~BIT(pin);
  }

  ret = i2c_write_byte_data(out_reg, dev.outreg_cache[port]);

  if (ret < 0) {
    DLOG("can't read output port%u\n", port);
  }
}

int
cy8c9540a_gpio_set_drive(unsigned gpio, unsigned mode)
{
  s32 ret = 0;
  u8 port = 0, pin = 0, offs = 0, val = 0;
  port = cypress_get_port(gpio);
  pin = cypress_get_offs(gpio, port);

  switch(mode) {
    case GPIOF_DRIVE_PULLUP:
      offs = 0x0;
      break;
    case GPIOF_DRIVE_STRONG:
      offs = 0x4;
      break;
    case GPIOF_DRIVE_HIZ:
      offs = 0x6;
      break;
    default:
      return -1;
  }

  ret = i2c_write_byte_data(REG_PORT_SELECT, port);
  if (ret < 0) {
    DLOG("can't select port%u\n", port);
    return ret;
  }

  ret = i2c_read_byte_data(REG_DRIVE_PULLUP + offs);
  if (ret < 0) {
    DLOG("can't read drive mode port%u\n", port);
    return ret;
  }

  val = (u8)(ret | BIT(pin));

  ret = i2c_write_byte_data(REG_DRIVE_PULLUP + offs, val);
  if (ret < 0) {
    DLOG("can't write drive mode port%u\n", port);
    return ret;
  }

  return 0;
}

int
cy8c9540a_gpio_direction(unsigned gpio, int out, int val)
{
  s32 ret = 0;
  u8 pins = 0, port = 0, pin = 0;
  port = cypress_get_port(gpio);

  if (out) {
    cy8c9540a_gpio_set_value(gpio, val);
  }

  ret = i2c_write_byte_data(REG_PORT_SELECT, port);
  if (ret < 0) {
    DLOG("can't select port%u\n", port);
    return ret;
  }

  ret = i2c_read_byte_data(REG_PIN_DIR);
  if (ret < 0) {
    DLOG("can't read pin direction\n", port);
    return ret;
  }

  pin = cypress_get_offs(gpio, port);

  pins = (u8)ret & 0xff;
  if (out) {
    pins &= ~BIT(pin);
  } else {
    pins |= BIT(pin);
  }
  
  ret = i2c_write_byte_data(REG_PIN_DIR, pins);
  if (ret < 0) {
    DLOG("can't write pin direction\n", port);
    return ret;
  }

  return 0;
}

int cy8c9540a_gpio_direction_output(unsigned gpio, int val)
{
  return cy8c9540a_gpio_direction(gpio, 1, val);
}

int cy8c9540a_gpio_direction_input(unsigned gpio)
{
  return cy8c9540a_gpio_direction(gpio, 0, 0);
}

s32 cypress_get_id()
{
  s32 dev_id = i2c_read_byte_data(REG_DEVID_STAT);
  return dev_id & 0xf0;
}

void cy8c9540a_test()
{
  unsigned gpio = 11;

  cy8c9540a_gpio_direction_output(gpio, 0);
  cy8c9540a_gpio_set_drive(gpio, GPIOF_DRIVE_STRONG);
  cy8c9540a_gpio_set_value(gpio, 1);
	i2c_remove();
}

bool cy8c9540a_setup()
{
	int ret = 0;
	int i = 0;
  s32 dev_id;
	const u8 eeprom_enable_seq[] = {0x43, 0x4D, 0x53, 0x2};

	/* enable i2c device */
	i2c_xfer_init(dev.addr);

  dev_id = cypress_get_id();
  DLOG("dev_id is 0x%x\n", dev_id);

	/* Disable PWM, set all GPIOs as input.  */
	for (i = 0; i < NPORTS; i++) {
		ret = i2c_write_byte_data(REG_PORT_SELECT, i);
		if (ret < 0) {
			DLOG("can't select port %u\n", i);
      return FALSE;
		}

		ret = i2c_write_byte_data(REG_SELECT_PWM, 0x00);
		if (ret < 0) {
			DLOG("can't write to SELECT_PWM\n");
      return FALSE;
		}

		ret = i2c_write_byte_data(REG_PIN_DIR, 0xff);
		if (ret < 0) {
			DLOG("can't write to PIN_DIR\n");
      return FALSE;
		}
	}

	/* Cache the output registers */
	ret = i2c_read_block_data(REG_OUTPUT_PORT0,
            sizeof(dev.outreg_cache),
            dev.outreg_cache);
	if (ret < 0) {
    DLOG("can't cache output registers\n");
    return ret;
	}

#if 0
	/* Set default PWM clock source.  */
	for (i = 0; i < NPWM; i ++) {
		ret = i2c_write_byte_data(REG_PWM_SELECT, i);
		if (ret < 0) {
			DLOG("can't select pwm %u\n", i);
      return ret;
		}

		ret = i2c_write_byte_data(REG_PWM_CLK, PWM_CLK);
		if (ret < 0) {
			DLOG("can't write to REG_PWM_CLK\n");
      return ret;
		}
	}
#endif

	/* Enable the EEPROM */
	ret = i2c_write_block_data(REG_ENABLE,
					     sizeof(eeprom_enable_seq),
					     eeprom_enable_seq);
	if (ret < 0) {
		DLOG("can't enable EEPROM\n");
    return ret;
	}

	cy8c9540a_test();

	return TRUE;
}

#include "module/header.h"

static const struct module_ops mod_ops = {
  .init = cy8c9540a_setup
};

DEF_MODULE (galileo_cy8c9540a, "Galileo CY8C9540A driver", &mod_ops, {"galileo_i2c"});


