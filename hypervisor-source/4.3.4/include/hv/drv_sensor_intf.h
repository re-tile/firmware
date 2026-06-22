/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 */

/**
 * @file
 * Interface definitions for the temperature sensor and fan controller driver.
 */

#ifndef _SYS_HV_DRV_SENSOR_INTF_H
#define _SYS_HV_DRV_SENSOR_INTF_H

/** Max label string length. */
#define SENSOR_LABEL_LENGTH     16

/** Offset for the temperature sensor channel number. */
#define SENSOR_TEMP_NR_OFF      0xF0000000

/** Offset for the fan regulator channel number. */
#define SENSOR_FAN_NR_OFF       0xF0010000

/** Offset for the sensor chip data. */
#define SENSOR_DATA_OFF         0xF0020000

/** Width of the sensor register, in bytes. */
#define SENSOR_REG_WIDTH        sizeof (int32_t)

/**
 * Composed offset bitfields:
 *  Bits [31:14] the offset base.
 *  Bits [13:9]  the temperature sensor or fan regulator index.
 *  Bits [8:0]   the register address.
 */
#define SENSOR_INDEX_SHIFT      9     /**< The sensor/regulator index shift. */
#define SENSOR_INDEX_RMASK      0x1f  /**< The sensor/regulator index mask. */
#define SENSOR_REG_SHIFT        0     /**< The register address shift. */
#define SENSOR_REG_RMASK        0x1ff /**< The register address mask. */

/** Extract the offset base from the composed offset. */
#define SENSOR_OFFSET_BASE(x)   ((x) & 0xffffc000)

/** Extract the temperature sensor or fan regulator index from the
 * composed offset.
 */
#define SENSOR_CHANNEL_INDEX(x) \
        (((x) >> SENSOR_INDEX_SHIFT) & SENSOR_INDEX_RMASK)

/** Extract the register index from the composed offset. */
#define SENSOR_REG_ADDR(x)      \
        ((((x) >> SENSOR_REG_SHIFT) & SENSOR_REG_RMASK) & \
         ~(SENSOR_REG_WIDTH - 1))

/** Compose an offset value using the offset base, temperature sensor or fan
 * regulator index, and register index.
 */
#define SENSOR_COMPOSE_OFFSET(x, idx, reg) \
        ((x) | ((idx) << SENSOR_INDEX_SHIFT) | ((reg) << SENSOR_REG_SHIFT))


/** Register index bit[8] specifies the register type. Bit[8] set means it's a
 * fan regulator register, clear means it's a temperature sensor register.
 */
#define SENSOR_REG_TYPE_FAN     0x100

/** Temperature sensor and fan regulator register address. */
/** The sensor chip configuration.  For hypervisor sensor driver internal use
 * only, can't access in the client OS. */
#define SENSOR_CHIP_CONF        0x00

/** The suggested label string of the temperature sensor. Read only. */
#define SENSOR_TEMP_LABEL       0x10

/** The current temperature input in Kelvin. Read only. */
#define SENSOR_TEMP_INPUT       (SENSOR_TEMP_LABEL + SENSOR_LABEL_LENGTH)

/** The temperature OT setting in Kelvin. Read write.
 * When a measured temperature exceeds the corresponding OT threshold, the OT
 * output pin asserts, and if any fan channel is controlled by the temperature
 * sensor in auto mode, the fan spins at max speed.
 */
#define SENSOR_TEMP_CRIT        (SENSOR_TEMP_INPUT + SENSOR_REG_WIDTH)

/** The temperature threshold in Kelvin fan start to spin in auto mode.
 * Read write.  When a measured temperature exceeds the corresponding
 * threshold, fan start spin at low speed, fan completely off if the measured
 * temperature drop 5 Celsius degree below the threshold.
 */
#define SENSOR_TEMP_FSTART      (SENSOR_TEMP_CRIT + SENSOR_REG_WIDTH)

/** The temperature threshold in Kelvin fan spin at max speed in auto mode.
 * Read write.  Fan spin at max speed when a measured temperature exceeds the
 * corresponding threshold.
 */
#define SENSOR_TEMP_FFULL       (SENSOR_TEMP_FSTART + SENSOR_REG_WIDTH)

/** The suggested label string of the fan regulator. Read only. */
#define SENSOR_FAN_LABEL        (SENSOR_REG_TYPE_FAN | 0x00)

/** The current fan revolution speed in RPM. Read only. */
#define SENSOR_FAN_INPUT        (SENSOR_FAN_LABEL + SENSOR_LABEL_LENGTH)

/** Fan speed control method of the fan regulator. Read write.
 *  The value:
 *   0  Manual RPM mode, fan speed is controled by the fan target speed setting
 *      in the 'SENSOR_FAN_TGT' register.
 *   1  Manual PWM mode, fan speed is controled by the pwm setting in the
 *      'SENSOR_FAN_PWM' register.
 *   2  Automatic PWM mode, fan speed is controled by the measured sensor
 *      temperature automatically.  Which temperature channels affect the PWM
 *      output is determined by the bitfields in the 'SENSOR_FAN_CTL' register.
 */
#define SENSOR_FAN_MODE         (SENSOR_FAN_INPUT + SENSOR_REG_WIDTH)

/** Fan PWM duty cycle, in range 0 to 255, 255 is max or 100%. Read write.
 * Writable when fan regulator in the manual PWM mode.
 */
#define SENSOR_FAN_PWM          (SENSOR_FAN_MODE + SENSOR_REG_WIDTH)

/** Desired fan target speed in RPM. Read write.
 * It's used in the manual RPM mode only, writable when fan regulator in the
 * manual RPM mode.
 */
#define SENSOR_FAN_TGT          (SENSOR_FAN_PWM + SENSOR_REG_WIDTH)

/** Fan max revolution speed in RPM. Read only. */
#define SENSOR_FAN_MAX          (SENSOR_FAN_TGT + SENSOR_REG_WIDTH)

/** Fan pulses per revolution, in range 1 to 4. Read write. */
#define SENSOR_FAN_PPR          (SENSOR_FAN_MAX + SENSOR_REG_WIDTH)

/** Fan regulator temperature sensor channels capability bitmap. Read only.
 * Used in the automatic PWM mode only. It shows which temperature
 * channels can affect the PWM output of this fan regulator, the bitfield,
 * 1 is temperature channel 1, 2 is temperature channel 2, 4 is temperature
 * channel 3, etc.
 */
#define SENSOR_FAN_CAP          (SENSOR_FAN_PPR + SENSOR_REG_WIDTH)

/** Fan regulator temperature sensor channels bitmap. Read write.
 * Used in the automatic PWM mode only. It select which temperature
 * channels affect the PWM output of this fan regulator, the bitfield,
 * 1 select temperature channel 1, 2 is temperature channel 2, 4 is
 * temperature channel 3, etc.
 * It must non-zero before switch the fan regulator to automatic PWM mode.
 */
#define SENSOR_FAN_CTL          (SENSOR_FAN_CAP + SENSOR_REG_WIDTH)

#endif /* _SYS_HV_DRV_SENSOR_INTF_H */
