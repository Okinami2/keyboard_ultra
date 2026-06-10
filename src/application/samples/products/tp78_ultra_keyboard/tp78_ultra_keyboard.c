#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"
#include "tp78_keyboard.h"
#include "tp78_matrix.h"
#include "tp78_rgb.h"
#include "tp78_usb_keyboard.h"

#define TP78_TASK_PRIORITY 24
#define TP78_TASK_STACK_SIZE 0x1400
#define TP78_SCAN_PERIOD_MS 1

static int tp78_keyboard_task(void *arg)
{
    unused(arg);

    if (tp78_usb_keyboard_init() != 0) {
        osal_printk("[tp78] USB initialization failed\r\n");
        return -1;
    }

    tp78_keyboard_init();
    tp78_rgb_init();
    osal_printk("[tp78] Ultra v2 ready, matrix 6x14, RGB 83 LEDs\r\n");

    while (true) {
        (void)tp78_matrix_scan();
        tp78_keyboard_process();
        osal_msleep(TP78_SCAN_PERIOD_MS);
    }

    return 0;
}

static void tp78_keyboard_entry(void)
{
    osal_task *task = NULL;

    osal_kthread_lock();
    task = osal_kthread_create(tp78_keyboard_task, NULL,
        "Tp78KeyboardTask", TP78_TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, TP78_TASK_PRIORITY);
    }
    osal_kthread_unlock();
}

app_run(tp78_keyboard_entry);
