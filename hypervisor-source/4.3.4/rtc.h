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
 * Routines to read and write the real-time hardware clock.
 */

#ifndef RTC_H
#define RTC_H

/** Typedef for rtc read time functions */
typedef int rtc_read_time_func(HV_RTCTime* tm);

/** Typedef for rtc write time functions */
typedef int rtc_write_time_func(HV_RTCTime tm);

/** Pointer to rtc read function */
extern rtc_read_time_func* rtc_read_time;

/** Pointer to rtc write function */
extern rtc_write_time_func* rtc_write_time;

/** RTC initialization function */
extern void init_rtc(void);

#endif
