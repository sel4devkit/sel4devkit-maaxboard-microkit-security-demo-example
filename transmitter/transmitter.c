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
#include <sel4/sel4.h>
#include <sel4_dma.h>
#include <uboot_drivers.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio_microkit.h>
#include <sel4_timer.h>
#include <plat/plat_support.h>
#include <mmc_platform_devices.h>
#include <circular_buffer.h>

#define LOG_FILE_DEVICE "mmc 0:1"  // Partition 1 on mmc device 0
#define LOG_FILENAME  "transmitter_log.txt"
#define LOG_FILE_WRITE_PERIOD_US 30000000  // Time between log file writes (30 seconds)


/* A buffer of encrypted characters to log to the SD/MMC card */
#define MMC_TX_BUF_LEN 4096
char mmc_pending_tx_buf[MMC_TX_BUF_LEN];
uint mmc_pending_length = 0;

/* Circular buffer state */
uintptr_t data_buffer;
size_t data_size = 0x10000;
uintptr_t circular_buffer;

/* DMA state */
static ps_dma_man_t dma_manager;
uintptr_t dma_base;
uintptr_t dma_cp_paddr;
size_t dma_size = 0x100000;

bool crypto_notified = false;

void write_pending_mmc_log()
{
    /* Track the total number of bytes written to the log file */
    static uint total_bytes_written = 0;

    /* Write all keypresses stored in the 'mmc_pending_tx_buf' buffer to the log file */
    char uboot_cmd[64];
    sprintf(uboot_cmd, "fatwrite %s 0x%x %s %x %x",
        LOG_FILE_DEVICE,        // The U-Boot partition designation
        &mmc_pending_tx_buf,    // Address of the buffer to write
        LOG_FILENAME,           // Filename to log to
        mmc_pending_length,     // The number of bytes to write
        total_bytes_written);   // The offset in the file to start writing from
    int ret = run_uboot_command(uboot_cmd);

    // Test string to read the file into
    char read_string[mmc_pending_length];

    // Read then output contents of the file
    sprintf(uboot_cmd, "fatload %s 0x%x %s %x %x",
        LOG_FILE_DEVICE,      // The U-Boot partition designation
        &read_string,         // Address to read the data into
        LOG_FILENAME,         // Filename to read from
        mmc_pending_length,   // Max number of bytes to read (0 = to end of file)
        total_bytes_written); // The offset in the file to start read from
    run_uboot_command(uboot_cmd);
    printf("String read from file %s: %s\n", LOG_FILENAME, read_string);

    /* Clear the buffer if writing to the file was successful */
    if (ret >= 0) {
        total_bytes_written += mmc_pending_length;

        /* All pending characters have now been sent. Clear the buffer */
        memset(mmc_pending_tx_buf, 0, mmc_pending_length);
        mmc_pending_length = 0;
    }

    printf("End of write_mmc_log\n");
}

void recieve_data_from_cypto(){
    circular_buffer_t* cb = (circular_buffer_t*)circular_buffer;
    while(!circular_buffer_empty(cb)){
        char encrypted_char = circular_buffer_get(cb, data_buffer, data_size);
        /* Store the read character in the buffer of pending data to log to SD/MMC. */
        /* If the buffer is full then discard the character */
        if (mmc_pending_length < MMC_TX_BUF_LEN) {
            mmc_pending_tx_buf[mmc_pending_length] = encrypted_char;
            mmc_pending_length += 1;
        }
    }
}

void
init(void)
{
    /* Initalise DMA manager */
    microkit_dma_manager(&dma_manager);

    /* Initialise DMA */
    microkit_dma_init(dma_base, dma_size,
        4096, 1);
    
    const char *const_dev_paths[] = DEV_PATHS;

    /* Initialise uboot library */
    initialise_uboot_drivers(
    dma_manager,
    incbin_device_tree_start,
    /* List the device tree paths for the devices */
    const_dev_paths, DEV_PATH_COUNT);

    /* Delete any existing log file to ensure we start with an empty file */
    char uboot_cmd[64];
    sprintf(uboot_cmd, "fatrm %s %s", LOG_FILE_DEVICE, LOG_FILENAME);
    run_uboot_command(uboot_cmd);

    /* Now poll for events and handle them */
    bool idle_cycle;
    unsigned long last_log_file_write_time = 0;
    while(true) {

        idle_cycle = true;

        /* Process encrypted characters and write to SD card */
        if ((uboot_monotonic_timer_get_us() - last_log_file_write_time) >= LOG_FILE_WRITE_PERIOD_US) {
            idle_cycle = false;
            recieve_data_from_cypto();
            if (mmc_pending_length > 0){
                write_pending_mmc_log();
                last_log_file_write_time = uboot_monotonic_timer_get_us();
            }
        }

        /* Sleep on idle cycles to prevent busy looping */
        if (idle_cycle) {
            seL4_Yield();
        }
    }
}

void
notified(microkit_channel ch)
{
    printf("Microkit notify crypto to transmitter on %d\n", ch);
    switch (ch) {
        case 6:
            crypto_notified = true;
            break;
        default:
            printf("crypto received protected unexpected channel\n");
    }
}