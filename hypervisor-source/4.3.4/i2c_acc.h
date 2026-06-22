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
 * Register definitions for the I2C master shim, and prototypes for our
 * access routines.
 */

#ifndef _SYS_HV_I2C_H
#define _SYS_HV_I2C_H

#include <arch/i2cm.h>
#include <arch/rsh.h>

#include "drvintf.h"

#include "hvbme/i2c_acc.h"

/** I2C master shim channel number; used only in the booter, since elsewhere
 *  we go by what we found during shim probe.
 * @param n Shim number; only useful on chips with more than one I2C master
 *   shim, like Gx.
 * @return Shim channel number.
 */
#define I2CMS_CHAN(n) \
  ((uint64_t) (RSH_MMIO_ADDRESS_SPACE__CHANNEL_VAL_I2CM0 + (n)) << \
               RSH_MMIO_ADDRESS_SPACE__CHANNEL_SHIFT)

//
// I2C access routines.
//
void i2c_enable(pos_t pos, unsigned long chan);
void i2c_enable_bib(pos_t pos, unsigned long chan);
int i2c_rd(pos_t pos, unsigned long chan, int dev, int addr, int len,
           void* buf);
int i2c_wr(pos_t pos, unsigned long chan, int dev, int addr, int len,
           void* buf);
int i2c_wrx(pos_t pos, unsigned long chan, int dev, int addr, int len,
            void* buf, int page_size, int write_cycle);

/**
 * I2C address of the Nth ROM device
 */
#define I2C_ROM_ADDR(n)  (0xA0 | ((n) << 1))

/**
 * Number of potential ROMs on an I2C bus
 */
#define I2C_NUM_ROMS     8

/**
 * I2C address of the GPIO expander on the PCI-E board
 */
#define I2C_PCIE_GPIO_ADDR        0x40

//
// I2C addresses of the GPIO expanders on a TILEblade-22G board
//
#define I2C_BLADE_GPIO00_ADDR        0x40      /**< Expander 0 */
#define I2C_BLADE_GPIO01_ADDR        0x42      /**< Expander 1 */
#define I2C_BLADE_GPIO10_ADDR        0x44      /**< Expander 2 */
#define I2C_BLADE_GPIO11_ADDR        0x46      /**< Expander 3 */

//
// Bits for the GPIO expander on a TILEblade
//
#define I2C_BLADE_GPIO11_0_O_PWRDOWN      0x01  /**< Out: Power Down    */
#define I2C_BLADE_GPIO11_0_O_SATA0_RST    0x02  /**< Out: SATA0 Reset   */
#define I2C_BLADE_GPIO11_0_O_SATA1_RST    0x04  /**< Out: SATA1 Reset   */
#define I2C_BLADE_GPIO11_0_O_GBE2_RST     0x08  /**< Out: Gbe2(Pcie) Reset */
#define I2C_BLADE_GPIO11_0_O_GBE2_AUXPWR  0x10  /**< Out: Gbe2(Pcie) Reset */
#define I2C_BLADE_GPIO11_0_O_GBE2_LAN0DIS 0x20  /**< Out: Gbe2(Pcie) Reset */
#define I2C_BLADE_GPIO11_0_O_GBE2_LAN1DIS 0x40  /**< Out: Gbe2(Pcie) Reset */
#define I2C_BLADE_GPIO11_0_O_GBE2_DEVOFF  0x80  /**< Out: Gbe2(Pcie) Reset */
#define I2C_BLADE_GPIO11_1_O_GBE0_HRST    0x04  /**< Out: GbE0 hw Reset */
#define I2C_BLADE_GPIO11_1_O_GBE0_SRST    0x08  /**< Out: GbE0 sw Reset */
#define I2C_BLADE_GPIO11_1_O_GBE1_HRST    0x10  /**< Out: GbE1 hw Reset */
#define I2C_BLADE_GPIO11_1_O_GBE1_SRST    0x20  /**< Out: GbE1 sw Reset */
#define I2C_BLADE_GPIO11_1_O_XGBE0_RST    0x40  /**< Out: XGbE0 Reset   */
#define I2C_BLADE_GPIO11_1_O_XGBE1_RST    0x80  /**< Out: XGbE1 Reset   */
#define I2C_BLADE_GPIO10_0_O_PLX_RST      0x01  /**< Out: PLX Reset     */

//
// Bits for the GPIO expander on the PCI-E board
//
#define I2C_PCIE_GPIO_0_I_SW_DONE   0x80  /**< In:  Switch init done status */
#define I2C_PCIE_GPIO_1_O_SW_RESET  0x20  /**< Out: Reset onboard switch */
#define I2C_PCIE_GPIO_1_O_VID_MASK  0x1F  /**< Out: voltage control bits */

//
// Registers in a PCA9555 I2C GPIO expander
//
#define I2C_GPIO_IN_0   0   /**< Input port 0 */
#define I2C_GPIO_IN_1   1   /**< Input port 1 */
#define I2C_GPIO_OUT_0  2   /**< Output port 0 */
#define I2C_GPIO_OUT_1  3   /**< Output port 1 */
#define I2C_GPIO_POL_0  4   /**< Input polarity port 0 (0 normal, 1 inverted) */
#define I2C_GPIO_POL_1  5   /**< Input polarity port 1 (0 normal, 1 inverted) */
#define I2C_GPIO_CFG_0  6   /**< Direction port 0 (1 input, 0 output) */
#define I2C_GPIO_CFG_1  7   /**< Direction port 1 (1 input, 0 output) */

//
// Registers in a LM84 I2C temperature sensor
//
#define I2C_LM84_R_LOC   0x0   /**< Read local temperature */
#define I2C_LM84_R_REM   0x1   /**< Read remote temperature */
#define I2C_LM84_R_STAT  0x2   /**< Read status */
#define I2C_LM84_R_CFG   0x3   /**< Read configuration */
#define I2C_LM84_R_MFGID 0x4   /**< Read manufacturer's ID */
#define I2C_LM84_R_L_SP  0x5   /**< Read local setpoint */
#define I2C_LM84_R_R_SP  0x7   /**< Read remote setpoint */
#define I2C_LM84_W_CFG   0x9   /**< Write configuration */
#define I2C_LM84_W_L_SP  0xb   /**< Write local setpoint */
#define I2C_LM84_W_R_SP  0xd   /**< Write remote setpoint */

//
// Registers in a LM95235 I2C temperature sensor, in the order in which
// they appear in the datasheet
//
#define I2C_LM95235_R_LOC_MSB        0x0  /**< Read MSB of local temp */
#define I2C_LM95235_R_LOC_LSB       0x30  /**< Read LSB of local temp */
#define I2C_LM95235_R_REM_MSB       0x01  /**< Read MSB of remote temp */
#define I2C_LM95235_R_REM_LSB       0x10  /**< Read LSB of remote temp */
#define I2C_LM95235_R_REM_UNS_MSB   0x31  /**< Read MSB of unsigned rem temp */
#define I2C_LM95235_R_REM_UNS_LSB   0x32  /**< Read LSB of unsigned rem temp */
#define I2C_LM95235_RW_CFG2         0xbf  /**< Read or write config2 */
#define I2C_LM95235_RW_REM_OFF_HB   0x11  /**< R/W remote offset high byte */
#define I2C_LM95235_RW_REM_OFF_LB   0x12  /**< R/W remote offset low byte */
#define I2C_LM95235_R_CFG1          0x03  /**< Read config1 */
#define I2C_LM95235_W_CFG1          0x09  /**< Write config1 */
#define I2C_LM95235_R_CONV_RATE     0x04  /**< Read conversion rate */
#define I2C_LM95235_W_CONV_RATE     0x0a  /**< Write conversion rate */
#define I2C_LM95235_W_ONESHOT       0x0f  /**< Write oneshot */
#define I2C_LM95235_R_STAT1         0x02  /**< Read status 1 */
#define I2C_LM95235_R_STAT2         0x33  /**< Read status 2 */
#define I2C_LM95235_R_REM_OS_LIM    0x07  /**< Read remote OT shutdown limit */
#define I2C_LM95235_W_REM_OS_LIM    0x0d  /**< Write remote OT shutdown lim */
#define I2C_LM95235_RW_LOC_LIM      0x20  /**< R/W local T_CRIT temp */
#define I2C_LM95235_RW_REM_CRIT_LIM 0x19  /**< R/W remote T_CRIT temp */
#define I2C_LM95235_RW_COMM_HYST    0x21  /**< R/W Common hysteresis */

//
// LM95235 Configuration Register 1 bits.
// 
#define I2C_LM95235_CFG1_LOC_OS_MASK     (1 << 1) /**< Local OS Mask */
#define I2C_LM95235_CFG1_LOC_TCRIT_MASK  (1 << 2) /**< Local TCRIT Mask */
#define I2C_LM95235_CFG1_REM_OS_MASK     (1 << 3) /**< Remote OS Mask */
#define I2C_LM95235_CFG1_REM_TCRIT_MASK  (1 << 4) /**< Remote TCRIT Mask */
#define I2C_LM95235_CFG1_STANDBY         (1 << 6) /**< Standby mode */

//
// LM95235 Configuration Register 2 bits.
// 
#define I2C_LM95235_CFG2_FILTER_MASK     (3 << 1) /**< Remote filter Mask */
#define I2C_LM95235_CFG2_TRUTHERM_MASK   (1 << 3) /**< Remote TruTherm Mask */
#define I2C_LM95235_CFG2_FLT_OS_MASK     (1 << 4) /**< Diode fault OS Mask */
#define I2C_LM95235_CFG2_FLT_TCRIT_MASK  (1 << 5) /**< Diode fault T_CRIT Msk */
#define I2C_LM95235_CFG2_OS_ENABLE_MASK  (1 << 6) /**< OTemp shutdown Mask */

//
// Registers in a MAX6639 I2C temperature sensor/fan controller, in the
// order in which they appear in the datasheet
//
#define I2C_MAX6639_CH1_TEMP         0x00 /**< Temperature channel 1 */
#define I2C_MAX6639_CH2_TEMP         0x01 /**< Temperature channel 2 */
#define I2C_MAX6639_STATUS           0x02 /**< Status byte */
#define I2C_MAX6639_OUT_MASK         0x03 /**< Output mask */
#define I2C_MAX6639_GLOBAL_CONFIG    0x04 /**< Global configuration */
#define I2C_MAX6639_CH1_EXT_TEMP     0x05 /**< Channel 1 extended temperature */
#define I2C_MAX6639_CH2_EXT_TEMP     0x06 /**< Channel 2 extended temperature */
#define I2C_MAX6639_CH1_ALERT_LIM    0x08 /**< Channel 1 ALERT limit */
#define I2C_MAX6639_CH2_ALERT_LIM    0x09 /**< Channel 2 ALERT limit */
#define I2C_MAX6639_CH1_OT_LIM       0x0A /**< Channel 1 OT limit */
#define I2C_MAX6639_CH2_OT_LIM       0x0B /**< Channel 2 OT limit */
#define I2C_MAX6639_CH1_THERM_LIM    0x0C /**< Channel 1 THERM limit */
#define I2C_MAX6639_CH2_THERM_LIM    0x0D /**< Channel 2 THERM limit */
#define I2C_MAX6639_CH1_FAN_CONF1    0x10 /**< Fan 1 configuration 1 */
#define I2C_MAX6639_CH1_FAN_CONF2A   0x11 /**< Fan 1 configuration 2a */
#define I2C_MAX6639_CH1_FAN_CONF2B   0x12 /**< Fan 1 configuration 2b */
#define I2C_MAX6639_CH1_FAN_CONF3    0x13 /**< Fan 1 configuration 3 */
#define I2C_MAX6639_CH2_FAN_CONF1    0x14 /**< Fan 2 configuration 1 */
#define I2C_MAX6639_CH2_FAN_CONF2A   0x15 /**< Fan 2 configuration 2a */
#define I2C_MAX6639_CH2_FAN_CONF2B   0x16 /**< Fan 2 configuration 2b */
#define I2C_MAX6639_CH2_FAN_CONF3    0x17 /**< Fan 2 configuration 3 */
#define I2C_MAX6639_CH1_TACH_CNT     0x20 /**< Fan 1 tachometer count */
#define I2C_MAX6639_CH2_TACH_CNT     0x21 /**< Fan 2 tachometer count */
#define I2C_MAX6639_CH1_TGT_TACH     0x22 /**< Fan 1 start/target tach count */
#define I2C_MAX6639_CH2_TGT_TACH     0x23 /**< Fan 2 start/target tach count */
#define I2C_MAX6639_CH1_PPR_MT       0x24 /**< Fan 1 pulse/rev,min tach count */
#define I2C_MAX6639_CH2_PPR_MT       0x25 /**< Fan 2 pulse/rev,min tach count */
#define I2C_MAX6639_CH1_DUTY_CYC     0x26 /**< Fan 1 cur/target duty cycle */
#define I2C_MAX6639_CH2_DUTY_CYC     0x27 /**< Fan 2 cur/target duty cycle */
#define I2C_MAX6639_CH1_MIN_FS_TEMP  0x28 /**< Chan 1 min fan-start temp */
#define I2C_MAX6639_CH2_MIN_FS_TEMP  0x29 /**< Chan 2 min fan-start temp */
#define I2C_MAX6639_DEV_ID           0x3D /**< Device ID */
#define I2C_MAX6639_MFG_ID           0x3E /**< Manufacturer ID */
#define I2C_MAX6639_DEV_REV          0x3F /**< Device revision */

//
// Registers in a PCF8563 real-time clock
//
#define I2C_PCF8563_STAT1      0x00  /**< Control and status 1 */
#define I2C_PCF8563_STAT2      0x01  /**< Control and status 2 */
#define I2C_PCF8563_VL_SEC     0x02  /**< VL flag; Seconds, 00 to 59, in BCD */
#define I2C_PCF8563_MIN        0x03  /**< Minutes, 00 to 59, in BCD */
#define I2C_PCF8563_HOUR       0x04  /**< Hours, 00 to 23, in BCD */
#define I2C_PCF8563_DAY        0x05  /**< Days, 01 to 31, in BCD */
#define I2C_PCF8563_WKDAY      0x06  /**< Weekdays, 0 to 6, in BCD */
#define I2C_PCF8563_CENT_MON   0x07  /**< Century; Months, 01 to 12, in BCD */
#define I2C_PCF8563_YEAR       0x08  /**< Years, 00 to 99, in BCD */
#define I2C_PCF8563_MIN_ALRM   0x09  /**< Minute alarm, 00 to 59, in BCD */
#define I2C_PCF8563_HOUR_ALRM  0x0a  /**< Hour alarm, 00 to 23, in BCD */
#define I2C_PCF8563_DAY_ALRM   0x0b  /**< Day alarm, 01 to 31, in BCD */
#define I2C_PCF8563_WKDAY_ALRM 0x0c  /**< Weekday alarm, 0 to 6, in BCD */
#define I2C_PCF8563_CLKOUT_CTL 0x0d  /**< Clock out control */
#define I2C_PCF8563_TIMER_CTL  0x0e  /**< Timer control */
#define I2C_PCF8563_TIMER      0x0f  /**< Timer countdown value */

//
// Bits for the PCF8563 registers
//
#define I2C_PCF8563_VL_MASK   0x80  /**< Low voltage mask */
#define I2C_PCF8563_SEC_MASK  0x7f  /**< Seconds mask */
#define I2C_PCF8563_MIN_MASK  0x7f  /**< Minutes mask */
#define I2C_PCF8563_HOUR_MASK 0x3f  /**< Hours mask */
#define I2C_PCF8563_DAY_MASK  0x3f  /**< Hours mask */
#define I2C_PCF8563_CENT_MASK 0x80  /**< Century mask */
#define I2C_PCF8563_MON_MASK  0x1f  /**< Month mask */
#define I2C_PCF8563_YEAR_MASK 0xff  /**< Year mask */

//
// Bits for the PCF8563 Control and Status 2 register
//
#define I2C_PCF8563_TIMER_INTR_ENABLE 0x01  /**< Timer interrupt enabled. */
#define I2C_PCF8563_ALARM_INTR_ENABLE 0x02  /**< Alarm interrupt enabled. */
#define I2C_PCF8563_TIMER_FLAG        0x04  /**< Timer flag. */
#define I2C_PCF8563_ALARM_FLAG        0x08  /**< Alarm flag. */

//
// Bits for the PCF8563 Timer Control register
//
#define I2C_PCF8563_TIMER_FREQ_4096HZ       0x00  /**< Timer freq 4096 Hz. */
#define I2C_PCF8563_TIMER_FREQ_64HZ         0x01  /**< Timer freq 64 Hz. */
#define I2C_PCF8563_TIMER_FREQ_1HZ          0x02  /**< Timer freq 1 Hz. */
#define I2C_PCF8563_TIMER_FREQ_1_60TH_HZ    0x03  /**< Timer freq 1/60 Hz. */
#define I2C_PCF8563_TIMER_ENABLE            0x80  /**< Timer enable. */

//
// Bits for the PCF8563 Timer Countdown register
//
#define I2C_PCF8563_TIMER_MIN   0x01  /**< Timer min value: 1. */
#define I2C_PCF8563_TIMER_MAX   0xFF  /**< Timer max value: 255. */

#ifdef L1BOOT

/** Configure an I2C switch to enable a channel.  i2c_switch_release_boot()
 *  must be called when done, in case we need to release a shared I2C bus.
 * @param pos Address of the I2C master shim.
 * @param chan Channel number of the I2C master shim.
 * @param bus I2C bus number.
 * @param switch_inst Instance number of the switch to be swung; relative
 *   to bus.
 * @param switch_chan Switch channel to be enabled.
 * @return Zero if the func is successful, or a negative error.
 */
int i2c_switch_swing_boot(pos_t pos, unsigned long chan, int bus,
                          int switch_inst, int switch_chan);

/** Release any resources reserved as a result of i2c_switch_swing_boot().
 * @param pos Address of the I2C master shim.
 * @param chan Channel number of the I2C master shim.
 * @param bus I2C bus number.
 * @param switch_inst Instance number of the switch to be released;
 *   relative to bus.
 */
void i2c_switch_release_boot(pos_t pos, unsigned long chan, int bus,
                             int switch_inst);

#endif // L1BOOT

#endif /* _SYS_HV_I2C_H */
