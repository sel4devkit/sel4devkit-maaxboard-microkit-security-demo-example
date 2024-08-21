/*
 * Copyright 2021, Breakaway Consulting Pty. Ltd.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <microkit.h>
#include <sel4_dma.h>
#include <uboot_drivers.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio_microkit.h>
#include <sel4_timer.h>
#include <plat/plat_support.h>
#include <usb_platform_devices.h>

/* DMA state */
static ps_dma_man_t dma_manager;
uintptr_t dma_base;
uintptr_t dma_cp_paddr;
size_t dma_size = 0x100000;

void handle_keypress(void) {
    printf("Reading input from the USB keyboard:\n");

    while(true) {
        while (uboot_stdin_tstc() > 0) {
            char c = uboot_stdin_getc();
            printf("Received character: %c\n", c, stdout);
            microkit_ppcall(5, seL4_MessageInfo_new((uint64_t) c,1,0,0));
            udelay(10000);
        }
    }
}

void
init(void)
{
    const char *const_dev_paths[] = DEV_PATHS;

    /* Initalise DMA manager */
    microkit_dma_manager(&dma_manager);
    
    /* Initialise DMA */
    microkit_dma_init(dma_base, dma_size,
        4096, 1);

    /* Initialise uboot library */
    initialise_uboot_drivers(
    dma_manager,
    incbin_device_tree_start,
    /* List the device tree paths for the devices */
    const_dev_paths, DEV_PATH_COUNT);

    /* Set USB keyboard as input device */
    int ret = run_uboot_command("setenv stdin usbkbd");
    if (ret < 0) {
        assert(!"Failed to set USB keyboard as the input device");
    }

    /* Start the USB subsystem */
    ret = run_uboot_command("usb start");
    if (ret < 0) {
        assert(!"Failed to start USB driver");
    }
    
    handle_keypress();

    return 0;
}

void
notified(microkit_channel ch)
{
}