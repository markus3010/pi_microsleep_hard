// Hard Microsleep Library via System Timer for the Raspberry Pi
//
// Copyright (c) 2021 Benjamin Spencer
// ============================================================================
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
// ============================================================================

// Include C standard libraries:
#include <stdint.h> // C Standard integer types
#include <errno.h>  // C Standard for error conditions

// Include C POSIX libraries:
#include <sys/mman.h> // Memory management library

// Include header files:
#include "pi_microsleep_hard.h" // Macro definitions
#include "bcm.h"                // Physical address definitions
#include "map_peripheral.h"     // Map peripherals into virtual memory
#include "get_pi_version.h"     // Determines PI versions

// Virtual address of GPIO peripheral base:
volatile uintptr_t *sys_timer_virt_addr;

// Configured?
static int config_flag = 0;

// PWM controller register map:
struct sys_timer_reg_map {
    uint32_t cs;  // System timer control/status
    uint32_t clo; // System timer counter (lower 32 bits)
    uint32_t chi; // System time counter (higher 32 bits)
    uint32_t c0;  // System timer compare 0 (USED BY GPU)
    uint32_t c1;  // System timer compare 1 (Typically free)
    uint32_t c2;  // System timer compare 2 (USED BY GPU)
    uint32_t c3;  // System timer compare 3 (Typically free)
};

static volatile struct sys_timer_reg_map *sys_timer_reg; // Arm timer reg map

// Configure for GPIO by mapping peripheral into virtual address space
int setup_microsleep_hard(void) {
    int pi_version;

    int bcm_peri_base_phys_addr;
    int sys_timer_phys_addr;

    // Fail silently if microsleep_hard has already been setup. We cannot remap
    // registers into virtual memory again!
    if (config_flag) {
        return 0;
    }

    // Get PI version by parsing /proc/cpu_info:
    pi_version = get_pi_version__();

    // Set BCM base addresses according to the found PI version:
    if ((pi_version == 0) || (pi_version == 1)) {
        // Set BCM base addresses:
        bcm_peri_base_phys_addr = BCM2835_PERI_BASE_PHYS_ADDR;
    } else if ((pi_version == 2) || (pi_version == 3)) {
        // Set BCM base addresses:
        bcm_peri_base_phys_addr = BCM2837_PERI_BASE_PHYS_ADDR;
    } else if (pi_version == 4) {
        // Set BCM base addresses:
        bcm_peri_base_phys_addr = BCM2711_PERI_BASE_PHYS_ADDR;
    } else  if (pi_version == 5){
        // Set BCM base addresses:
        bcm_peri_base_phys_addr = BCM2712_PERI_BASE_PHYS_ADDR;
    } else {
        return -ENOPIVER;
    }

    // Add in offset to find registers of value:
    // (Points to LOAD register)
    sys_timer_phys_addr = (bcm_peri_base_phys_addr + BCM_SYS_TIMER_BASE_OFFSET);

    // Map physical address into virtual address space to manipulate registers:
    sys_timer_virt_addr = map_peripheral__(sys_timer_phys_addr);

    if ((void *) sys_timer_virt_addr == MAP_FAILED) {
        return (int) MAP_FAILED;
    }

    // Set flag so only one mapping is done:
    config_flag = 1;

    // Point register map structure to virtual address of
    // ARM timer register base:
    sys_timer_reg = (struct arm_timer_reg_map*) (sys_timer_virt_addr);

    return 0;
}

int microsleep_hard(unsigned int usec) {
    uint32_t timout;

    // Setup microsleep if it hasn't already been explicitly set up for us.
    // Memory mapping is required for this to work so register mapping
    // has to be done sometime.
    if (!(config_flag)) {
        setup_microsleep_hard();
    }

    // My perferred method of using the system timer is to compare against
    // the free running timer directly instead of relying upon interrupts.
    // This method is guaranteed to work all the way down to 1 us delays.
    // See the comment block below on how to implement the intended interrupt
    // driven approach and some of the limitations I found.

    // Add desired delay time to current value of the free running counter
    // to get when we should timeout:
    timout = sys_timer_reg->clo + usec;

    // Spin while we wait for the clock to reach timout time:
    while (sys_timer_reg->clo < timout);

    /*

    The inefficient way to do it (because the additional if).

    // Between 12 and 25 usec delay, interrupt is not generated by the system
    // timer reliably and we must rely on comparing the free running counter
    // ourselves. I would guess this is a limitation of Linux scheduler and
    // it interrupt sharing. 
    if (usec < 13) {
        // Add to current value of the timer to find what our timeout time will be
        timout = sys_timer_reg->clo + usec;

        // Spin while we wait for the timer to tick:
        while (sys_timer_reg->clo < timout);
    } else {
        // This will clear the interrupt pending bit for C3 specifically:
        sys_timer_reg->cs = 0x8;

        // Set the compare time to current timer + desired sleep:
        sys_timer_reg->c3 = sys_timer_reg->clo + usec;

        // Spin while interrupt pending bit is cleared; it will be set once the
        // timer elapses:
        while (((sys_timer_reg->cs & 0x8) >> 3) == 0);
    }

    */

    return 0;
}
