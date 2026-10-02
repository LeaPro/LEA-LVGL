/**
 *
 * @file hid_encoder.c
 *
 * Driver backend for the Trevally rotary encoder dial.
 *
 * Reads raw USB HID reports from a custom Eberwein Hall-effect (AS5600)
 * dial over hidapi and exposes it as a standard LV_INDEV_TYPE_ENCODER
 * input device through the driver_backends abstraction, the same way
 * evdev.c exposes a pointer device for touch/mouse. The report layout
 * (VID/PID, struct, bitmasks) was cross-checked against a known-working
 * decoder for the same chip used by another LEA product line.
 *
 * Author: EDGEMTech Ltd, Erik Tagirov (erik.tagirov@edgemtech.ch)
 *
 * Copyright (c) 2025 EDGEMTech Ltd.
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "lvgl/lvgl.h"
#if LV_USE_HID_ENCODER
#include <hidapi/hidapi.h>

#include "../backends.h"

/*********************
 *      DEFINES
 *********************/

#define HID_USB_VID 0x0000
#define HID_USB_PID 0x0002
#define HID_REPORT_ID_1 0x42

/* Empirically determined on target hardware: raw_and_button's low byte
 * (buf[1]) is a direction/button flag byte (0x20 observed while turning
 * CW, 0x40 while turning CCW, 0x80 while pressed); the high byte
 * (buf[2]) is a free-running relative position counter that increments
 * CW / decrements CCW, not a button/direction field. */
#define HID_INPUT_BUTTON_MASK    0x0080
#define HID_INPUT_DIRECTION_MASK 0x0060
#define HID_INPUT_DIRECTION_SHIFT 5
#define HID_INPUT_DIRECTION_POS  0x1
#define HID_INPUT_DIRECTION_NEG  0x2

/**********************
 *      TYPEDEFS
 **********************/

typedef struct __attribute__((__packed__)) {
    uint8_t id;
    uint16_t raw_and_button;
    uint8_t dial_id;
    uint8_t nr_dial_pos;
} hid_input_report_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static lv_indev_t * init_hid_encoder(lv_display_t * display);
static void hid_encoder_read(lv_indev_t * indev, lv_indev_data_t * data);
static void hid_encoder_deleted_cb(lv_event_t * e);
static bool hid_encoder_open(void);

/**********************
 *  STATIC VARIABLES
 **********************/

static char * backend_name = "HID_ENCODER";
static hid_device * g_hid = NULL;
static bool g_pressed = false;

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/*
 * Initialize the HID encoder driver
 *
 * @param backend the backend descriptor
 */
int backend_init_hid_encoder(backend_t * backend)
{
    LV_ASSERT_NULL(backend);
    backend->handle->indev = malloc(sizeof(indev_backend_t));
    LV_ASSERT_NULL(backend->handle->indev);

    backend->handle->indev->init_indev = init_hid_encoder;

    backend->name = backend_name;
    backend->type = BACKEND_INDEV;
    return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/*
 * Open the HID device if it isn't already open
 *
 * @return true if the device is open (either already, or just now)
 */
static bool hid_encoder_open(void)
{
    if(g_hid != NULL) {
        return true;
    }

    /* hid_open() performs a full USB enumeration scan. The indev read
     * callback runs on every LVGL indev read tick (~16ms, ~60/sec by
     * default) so retrying on every call while the dial is absent/not
     * yet enumerated would scan USB ~60x/sec forever. Throttle retries
     * and log the outcome so a missing/misconfigured device is visible
     * instead of silently doing nothing. */
    static uint32_t last_attempt_tick = 0;
    static bool first_attempt = true;

    if(!first_attempt && lv_tick_elaps(last_attempt_tick) < 500) {
        return false;
    }
    first_attempt = false;
    last_attempt_tick = lv_tick_get();

    g_hid = hid_open(HID_USB_VID, HID_USB_PID, NULL);

    if(g_hid != NULL) {
        hid_set_nonblocking(g_hid, 1);
        printf("hid_encoder: opened device %04x:%04x\n", HID_USB_VID, HID_USB_PID);
    }
    else {
        printf("hid_encoder: hid_open(%04x:%04x) failed: %ls\n",
               HID_USB_VID, HID_USB_PID, hid_error(NULL));
    }

    return g_hid != NULL;
}

/*
 * Release the HID device
 *
 * @description called by LVGL when the indev is deleted
 * @param e the deletion event
 */
static void hid_encoder_deleted_cb(lv_event_t * e)
{
    (void)e;

    if(g_hid != NULL) {
        hid_close(g_hid);
        g_hid = NULL;
    }
}

/*
 * Read callback for the HID rotary encoder
 *
 * Drains all pending reports since the last call (non-blocking), decodes
 * the button-pressed bit and rotation direction bits out of report 0x42
 * and reports the accumulated step count as enc_diff. Same single
 * threaded, once-per-lv_timer_handler()-tick pattern as LVGL's own evdev
 * and SDL mousewheel drivers.
 *
 * @param indev the input device
 * @param data the data to populate
 */
static void hid_encoder_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    (void)indev;
    int16_t diff = 0;

    if(!hid_encoder_open()) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->enc_diff = 0;
        return;
    }

    uint8_t buf[64];
    int res;

    while((res = hid_read(g_hid, buf, sizeof(buf))) > 0) {
        if(res >= (int)sizeof(hid_input_report_t) && buf[0] == HID_REPORT_ID_1) {
            hid_input_report_t * report = (hid_input_report_t *)buf;
            uint16_t rab = report->raw_and_button;

            g_pressed = (rab & HID_INPUT_BUTTON_MASK) != 0;

            unsigned dir = (rab & HID_INPUT_DIRECTION_MASK) >> HID_INPUT_DIRECTION_SHIFT;

            if(dir == HID_INPUT_DIRECTION_POS) {
                diff++;
            }
            else if(dir == HID_INPUT_DIRECTION_NEG) {
                diff--;
            }
        }
    }

    if(res < 0) {
        /* device unplugged or read error - close and retry on a later tick */
        hid_close(g_hid);
        g_hid = NULL;
    }

    data->state = g_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->enc_diff = diff;
}

/*
 * Initialize the HID rotary encoder as an LV_INDEV_TYPE_ENCODER device
 *
 * @param display the LVGL display to attach the indev to
 * @return the input device
 */
static lv_indev_t * init_hid_encoder(lv_display_t * display)
{
    hid_init();

    lv_indev_t * indev = lv_indev_create();

    if(indev == NULL) {
        return NULL;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(indev, hid_encoder_read);
    lv_indev_set_display(indev, display);

    /* Use LVGL's native encoder semantics: rotate to move focus within the
     * default group, press to select the focused widget - the same group
     * wiring the SDL keyboard/mousewheel indevs use in
     * display_backends/sdl.c. */
    lv_indev_set_group(indev, lv_group_get_default());

    lv_indev_add_event_cb(indev, hid_encoder_deleted_cb, LV_EVENT_DELETE, NULL);

    return indev;
}

#endif /*#if LV_USE_HID_ENCODER*/
