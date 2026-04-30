/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * cam_sensor_uapi.h - Spacemit K1X Camera Sensor UAPI definitions
 *
 * Copyright(C) 2023 SPACEMIT Micro Limited.
 */

#ifndef __CAM_SENSOR_UAPI_H__
#define __CAM_SENSOR_UAPI_H__

#include <linux/types.h>
#include <linux/ioctl.h>

/* Maximum number of sensor devices */
#define CAM_SNS_MAX_DEV_NUM		8

/* I2C data length types */
enum sensor_i2c_len {
	I2C_8BIT = 1,
	I2C_16BIT = 2,
	I2C_24BIT = 3,
	I2C_32BIT = 4,
};

/* Sensor power regulator identifiers */
typedef enum {
	SENSOR_REGULATOR_AFVDD = 0,
	SENSOR_REGULATOR_AVDD,
	SENSOR_REGULATOR_DOVDD,
	SENSOR_REGULATOR_DVDD,
	SENSOR_REGULATOR_MAX,
} cam_sensor_power_regulator_id;

/* Sensor GPIO identifiers */
typedef enum {
	SENSOR_GPIO_PWDN = 0,
	SENSOR_GPIO_RST,
	SENSOR_GPIO_DVDDEN,
	SENSOR_GPIO_DCDCEN,
	SENSOR_GPIO_MAX,
} cam_sensor_gpio_id;

/**
 * struct cam_sensor_power - Sensor power control structure
 * @regulator_id: Which power regulator to control
 * @voltage: Voltage in microvolts (for voltage setting)
 * @on: 1 to enable, 0 to disable (for power on/off)
 */
struct cam_sensor_power {
	cam_sensor_power_regulator_id regulator_id;
	__u32 voltage;
	__u32 on;
};

/**
 * struct cam_sensor_gpio - Sensor GPIO control structure
 * @gpio_id: Which GPIO to control
 * @enable: 1 to set high, 0 to set low
 */
struct cam_sensor_gpio {
	cam_sensor_gpio_id gpio_id;
	__u8 enable;
};

/**
 * struct regval_tab - Register value table entry
 * @reg: Register address
 * @val: Register value
 */
struct regval_tab {
	__u16 reg;
	__u32 val;
};

/**
 * struct cam_i2c_data - Camera I2C data for single register access
 * @reg_len: Register address length in bytes
 * @val_len: Value length in bytes
 * @addr: 7-bit I2C slave address
 * @tab: Register/value pair
 */
struct cam_i2c_data {
	enum sensor_i2c_len reg_len;
	enum sensor_i2c_len val_len;
	__u8 addr;
	struct regval_tab tab;
};

/**
 * struct cam_burst_i2c_data - Camera I2C data for burst register access
 * @reg_len: Register address length in bytes
 * @val_len: Value length in bytes
 * @addr: 7-bit I2C slave address
 * @num: Number of register entries
 * @tab: Pointer to array of register/value pairs
 */
struct cam_burst_i2c_data {
	enum sensor_i2c_len reg_len;
	enum sensor_i2c_len val_len;
	__u8 addr;
	__u32 num;
	struct regval_tab __user *tab;
};

/**
 * struct cam_sensor_info - Camera sensor information
 * @twsi_no: I2C/TWSI bus number
 */
struct cam_sensor_info {
	__u8 twsi_no;
};

/* MIPI clock frequency type */
typedef __u32 sns_mipi_clock_t;

/* Sensor reset source type (sensor index) */
typedef __u32 sns_rst_source_t;

/**
 * struct cam_power_voltage - Power voltage configuration
 * @power_type: Power rail type (AVDD, DOVDD, DVDD, AFVDD)
 * @voltage: Voltage in microvolts
 */
struct cam_power_voltage {
	__u32 power_type;
	__u32 voltage;
};

/**
 * struct cam_power_on - Power on/off control
 * @power_type: Power rail type
 * @on: 1 to enable, 0 to disable
 */
struct cam_power_on {
	__u32 power_type;
	__u32 on;
};

/**
 * struct cam_gpio_enable - GPIO enable control
 * @gpio_type: GPIO type (reset, powerdown, etc.)
 * @enable: 1 to enable, 0 to disable
 */
struct cam_gpio_enable {
	__u32 gpio_type;
	__u32 enable;
};

/**
 * struct cam_mclk_rate - MCLK rate configuration
 * @rate: Clock rate in Hz
 */
struct cam_mclk_rate {
	__u32 rate;
};

/**
 * struct cam_mclk_enable - MCLK enable control
 * @enable: 1 to enable, 0 to disable
 */
struct cam_mclk_enable {
	__u32 enable;
};

/* Camera sensor ioctl magic number */
#define CAM_SENSOR_IOC_MAGIC		'S'

/* Sensor ioctl command numbers */
#define SENSOR_IOC_RESET		0x01
#define SENSOR_IOC_UNRESET		0x02
#define SENSOR_IOC_I2C_WRITE		0x03
#define SENSOR_IOC_I2C_READ		0x04
#define SENSOR_IOC_I2C_BURST_WRITE	0x05
#define SENSOR_IOC_I2C_BURST_READ	0x06
#define SENSOR_IOC_GET_INFO		0x07
#define SENSOR_IOC_SET_MIPI_CLOCK	0x08
#define SENSOR_IOC_SET_POWER_VOLTAGE	0x09
#define SENSOR_IOC_SET_POWER_ON		0x0A
#define SENSOR_IOC_SET_GPIO_ENABLE	0x0B
#define SENSOR_IOC_SET_MCLK_RATE	0x0C
#define SENSOR_IOC_SET_MCLK_ENABLE	0x0D

/* Camera sensor ioctl commands */
#define CAM_SENSOR_RESET		_IO(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_RESET)
#define CAM_SENSOR_UNRESET		_IO(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_UNRESET)
#define CAM_SENSOR_I2C_WRITE		_IOW(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_I2C_WRITE, struct cam_i2c_data)
#define CAM_SENSOR_I2C_READ		_IOWR(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_I2C_READ, struct cam_i2c_data)
#define CAM_SENSOR_I2C_BURST_WRITE	_IOW(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_I2C_BURST_WRITE, struct cam_burst_i2c_data)
#define CAM_SENSOR_I2C_BURST_READ	_IOWR(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_I2C_BURST_READ, struct cam_burst_i2c_data)
#define CAM_SENSOR_GET_INFO		_IOR(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_GET_INFO, struct cam_sensor_info)
#define CAM_SENSOR_SET_MIPI_CLOCK	_IOW(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_SET_MIPI_CLOCK, __u32)
#define CAM_SENSOR_SET_POWER_VOLTAGE	_IOW(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_SET_POWER_VOLTAGE, struct cam_sensor_power)
#define CAM_SENSOR_SET_POWER_ON		_IOW(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_SET_POWER_ON, struct cam_sensor_power)
#define CAM_SENSOR_SET_GPIO_ENABLE	_IOW(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_SET_GPIO_ENABLE, struct cam_sensor_gpio)
#define CAM_SENSOR_SET_MCLK_RATE	_IOW(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_SET_MCLK_RATE, struct cam_mclk_rate)
#define CAM_SENSOR_SET_MCLK_ENABLE	_IOW(CAM_SENSOR_IOC_MAGIC, SENSOR_IOC_SET_MCLK_ENABLE, struct cam_mclk_enable)

#endif /* __CAM_SENSOR_UAPI_H__ */
