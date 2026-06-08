#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "keyscan.h"
#include "tp78_keymap.h"
#include "tp78_usb_keyboard.h"

#define TP78_TASK_PRIORITY 24
#define TP78_TASK_STACK_SIZE 0x1000
#define TP78_MAX_KEYS 6
#define HID_MODIFIER_MIN 0xE0
#define HID_MODIFIER_MAX 0xE7

static int tp78_keyscan_callback(int argc, uint8_t argv[])
{
    uint8_t modifiers = 0;
    uint8_t keys[TP78_MAX_KEYS] = { 0 };
    uint8_t key_count = 0;

    for (int index = 0; index < argc; index++) {
        uint8_t key = argv[index];
        if (key >= HID_MODIFIER_MIN && key <= HID_MODIFIER_MAX) {
            modifiers |= (uint8_t)(1U << (key - HID_MODIFIER_MIN));
        } else if (key != 0 && key_count < TP78_MAX_KEYS) {
            keys[key_count++] = key;
        }
    }

    return tp78_usb_keyboard_send(modifiers, keys) < 0 ? 0 : 1;
}

static void *tp78_keyboard_task(const char *arg)
{
    unused(arg);

    if (tp78_usb_keyboard_init() != 0) {
        osal_printk("[tp78] USB keyboard initialization failed\r\n");
        return NULL;
    }

    uapi_set_keyscan_value_map(tp78_keymap_get(),
        CONFIG_KEYSCAN_ENABLE_ROW, CONFIG_KEYSCAN_ENABLE_COL);
    uapi_keyscan_init(EVERY_ROW_PULSE_40_US, HAL_KEYSCAN_MODE_0, KEYSCAN_INT_VALUE_RDY);
    uapi_keyscan_register_callback(tp78_keyscan_callback);
    uapi_keyscan_enable();
    osal_printk("[tp78] keyboard ready\r\n");
    return NULL;
}

static void tp78_keyboard_entry(void)
{
    osal_task *task = NULL;

    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)tp78_keyboard_task, NULL,
        "Tp78KeyboardTask", TP78_TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, TP78_TASK_PRIORITY);
    }
    osal_kthread_unlock();
}

app_run(tp78_keyboard_entry);
