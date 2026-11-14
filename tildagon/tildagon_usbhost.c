// DRAFT, uncompiled and untested. Tildagon USB host (CDC-ACM) MicroPython module.
// Place in drivers/tildagon_usb/ next to tildagon_usb.c and add it to tildagon_usb.cmake.
//
// Python API:
//   import usbhost
//   usbhost.start()          # power the port, route the S3 USB lines to J8, start host stack
//   usbhost.connected()      # True once a CDC device has enumerated
//   usbhost.write(b"...")    # -> bytes queued
//   usbhost.read(n=64, timeout_ms=0)  # -> bytes (may be empty)
//   usbhost.stop()           # stop host, switch the mux back to the device port

#include "py/runtime.h"
#include "py/obj.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "tildagon_pin/tildagon_pin.h"   // ext_pin[]
#include "tildagon_pin/aw9523b.h"
#include "tildagon_power/tildagon_power.h"

#define USB_SEL_PIN   5      // ext_pin[2] P0_5, drives TS3USB221A S: 0 = device port J7, 1 = host port J8
#define RX_BUF_SIZE   2048

static TaskHandle_t host_task = NULL;
static StreamBufferHandle_t rx_stream = NULL;
static cdc_acm_dev_hdl_t cdc_dev = NULL;
static volatile bool running = false;
static volatile bool dev_connected = false;

static void usb_lib_task(void *arg) {
    while (running) {
        uint32_t flags;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100), &flags);
    }
    vTaskDelete(NULL);
}

static bool rx_cb(const uint8_t *data, size_t len, void *arg) {
    xStreamBufferSend(rx_stream, data, len, 0);
    return true;
}

static void dev_event_cb(const cdc_acm_host_dev_event_data_t *ev, void *arg) {
    if (ev->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
        dev_connected = false;
        cdc_acm_host_close(ev->data.cdc_hdl);
        cdc_dev = NULL;
    }
}

static void new_dev_cb(usb_device_handle_t usb_dev) {
    // Open the first CDC-ACM device that appears (any VID/PID).
    const cdc_acm_host_device_config_t cfg = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 512,
        .in_buffer_size = 512,
        .event_cb = dev_event_cb,
        .data_cb = rx_cb,
        .user_arg = NULL,
    };
    // Needs a task context in real use; see README for the deferred-open pattern.
    if (cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0, &cfg, &cdc_dev) == ESP_OK) {
        dev_connected = true;
    }
}

static mp_obj_t usbhost_start(void) {
    if (running) return mp_const_none;

    // 1. Make sure the host port is powered (normally done by the attach handler).
    tildagon_power_enable_5v(true);

    // 2. Route the S3 USB pins to the host port.
    aw9523b_pin_set_output(&ext_pin[2], USB_SEL_PIN, true);

    // 3. TODO (riskiest step): stop the device-mode stack that owns the controller now
    //    (TinyUSB or USB-Serial-JTAG) before usb_host_install(). See README.

    rx_stream = xStreamBufferCreate(RX_BUF_SIZE, 1);
    running = true;

    const usb_host_config_t host_cfg = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    if (usb_host_install(&host_cfg) != ESP_OK) {
        running = false;
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("usb_host_install failed"));
    }
    xTaskCreatePinnedToCore(usb_lib_task, "usbh_lib", 4096, NULL, 10, &host_task, 0);
    cdc_acm_host_install(&(cdc_acm_host_driver_config_t){
        .driver_task_stack_size = 4096, .driver_task_priority = 11,
        .xCoreID = 0, .new_dev_cb = new_dev_cb });
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(usbhost_start_obj, usbhost_start);

static mp_obj_t usbhost_stop(void) {
    running = false;
    if (cdc_dev) { cdc_acm_host_close(cdc_dev); cdc_dev = NULL; }
    cdc_acm_host_uninstall();
    usb_host_uninstall();
    aw9523b_pin_set_output(&ext_pin[2], USB_SEL_PIN, false);
    dev_connected = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(usbhost_stop_obj, usbhost_stop);

static mp_obj_t usbhost_connected(void) { return mp_obj_new_bool(dev_connected); }
static MP_DEFINE_CONST_FUN_OBJ_0(usbhost_connected_obj, usbhost_connected);

static mp_obj_t usbhost_write(mp_obj_t data_in) {
    mp_buffer_info_t b;
    mp_get_buffer_raise(data_in, &b, MP_BUFFER_READ);
    if (!dev_connected || !cdc_dev) mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("no device"));
    if (cdc_acm_host_data_tx_blocking(cdc_dev, b.buf, b.len, 1000) != ESP_OK)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("tx failed"));
    return mp_obj_new_int(b.len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(usbhost_write_obj, usbhost_write);

static mp_obj_t usbhost_read(size_t n_args, const mp_obj_t *args) {
    size_t n = n_args > 0 ? mp_obj_get_int(args[0]) : 64;
    int timeout = n_args > 1 ? mp_obj_get_int(args[1]) : 0;
    if (!rx_stream) return mp_const_empty_bytes;
    vstr_t vstr; vstr_init_len(&vstr, n);
    size_t got = xStreamBufferReceive(rx_stream, vstr.buf, n, pdMS_TO_TICKS(timeout));
    vstr.len = got;
    return mp_obj_new_bytes_from_vstr(&vstr);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(usbhost_read_obj, 0, 2, usbhost_read);

static const mp_rom_map_elem_t usbhost_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_usbhost) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&usbhost_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&usbhost_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_connected), MP_ROM_PTR(&usbhost_connected_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&usbhost_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&usbhost_read_obj) },
};
static MP_DEFINE_CONST_DICT(usbhost_globals, usbhost_globals_table);

const mp_obj_module_t usbhost_module = { .base = { &mp_type_module }, .globals = (mp_obj_dict_t *)&usbhost_globals };
MP_REGISTER_MODULE(MP_QSTR_usbhost, usbhost_module);
