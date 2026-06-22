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

#include "hv.h"

        // Measure the number of cycles required to load from a VA on Gx.
        // This routine is aligned to a single I-cache line so that the
        // difference in cycle counts will measure the ld instruction
        // latency.

        .align 64
        .global measure_load_latency
        .section .text.measure_load_latency, "ax"
        .type measure_load_latency,@function
measure_load_latency:
        mfspr r1, CYCLE
        ld r2, r0
        {
         addi r2, r2, 0    // Stalls for load result
         mfspr r0, CYCLE
        }
        sub r0, r0, r1
        {
         addi r0, r0, -1   // Subtract off 1 because CYCLE is fast on Gx.
         jrp lr
        }
        .size measure_load_latency,.-measure_load_latency

        // Measure the number of cycles required to load from a PA on Gx.
        // Perform 16 measurements and take the average.

        .global measure_load_latency_phys
        .section .text.measure_load_latency_phys, "ax"
        .type measure_load_latency_phys,@function
measure_load_latency_phys:
        // measure_load_latency_phys(PA physaddr, unsigned long aar);
        {
	 movei r10, -1
	 mfspr r11, INTERRUPT_MASK_HV
	}
        mtspr  INTERRUPT_MASK_SET_HV, r10
	mtspr  AAR, r1
        {
         move   r5, zero
         movei  r4, 16      // loop 16 times.
        }
1:      mfspr  r2, CYCLE
        ld     r3, r0       // load from PA in r0.
        {
         addi  r3, r3, 0    // Stalls for load result.
         mfspr r1, CYCLE
        }
        {
         sub    r2, r1, r2   // get delta cycles.
         addi   r4, r4, -1
        }
        {
         add    r5, r5, r2
         bnezt  r4, 1b
        }
        addi r5, r5, 8    // Add 8 to total cycles before divide by 16 next.
        {
         mtspr  AAR, zero
         shrui  r0, r5, 4  // Div 16 to get average latency.
        }
        mtspr  INTERRUPT_MASK_HV, r11
        jrp    lr
        .size measure_load_latency_phys,.-measure_load_latency_phys
