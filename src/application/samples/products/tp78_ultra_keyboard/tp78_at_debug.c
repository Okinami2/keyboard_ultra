#include <stdbool.h>
#include <stdio.h>

#include "at.h"
#include "errcode.h"
#include "soc_osal.h"
#include "tp78_at_debug.h"
#include "tp78_oled.h"

#define TP78_AT_CMD_ID_OLED 0x7801
#define TP78_AT_CMD_ID_OLED_ON 0x7802
#define TP78_AT_CMD_ID_OLED_DATA1 0x7803
#define TP78_AT_CMD_ID_OLED_DATA16 0x7804
#define TP78_AT_CMD_ID_OLED_CURSOR 0x7805

#define TP78_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define TP78_OLED_AT_QUEUE_LEN 1
#define TP78_OLED_AT_TASK_PRIORITY 29
#define TP78_OLED_AT_TASK_STACK_SIZE 0x1000

typedef enum {
    TP78_OLED_AT_CMD_SHOW,
    TP78_OLED_AT_CMD_ON,
    TP78_OLED_AT_CMD_DATA1,
    TP78_OLED_AT_CMD_DATA16,
    TP78_OLED_AT_CMD_CURSOR,
} tp78_oled_at_cmd_t;

static unsigned long g_tp78_oled_at_queue;
static volatile bool g_tp78_oled_at_ready;
static volatile bool g_tp78_oled_at_busy;

static const char *tp78_oled_at_name(tp78_oled_at_cmd_t command)
{
    switch (command) {
        case TP78_OLED_AT_CMD_SHOW:
            return "TP78OLED";
        case TP78_OLED_AT_CMD_ON:
            return "TP78OLEDON";
        case TP78_OLED_AT_CMD_DATA1:
            return "TP78OLED1";
        case TP78_OLED_AT_CMD_DATA16:
            return "TP78OLED16";
        case TP78_OLED_AT_CMD_CURSOR:
            return "TP78OLEDCUR";
        default:
            return "TP78OLED";
    }
}

static int32_t tp78_oled_at_execute(tp78_oled_at_cmd_t command)
{
    switch (command) {
        case TP78_OLED_AT_CMD_SHOW:
            return tp78_oled_show_next_art();
        case TP78_OLED_AT_CMD_ON:
            return tp78_oled_debug_all_on();
        case TP78_OLED_AT_CMD_DATA1:
            return tp78_oled_debug_write_data(1);
        case TP78_OLED_AT_CMD_DATA16:
            return tp78_oled_debug_write_data(16);
        case TP78_OLED_AT_CMD_CURSOR:
            return tp78_oled_debug_cursor();
        default:
            return -1;
    }
}

static void tp78_oled_at_report(tp78_oled_at_cmd_t command, const char *status)
{
    char response[32];

    if (snprintf(response, sizeof(response), "+%s:%s\r\n", tp78_oled_at_name(command), status) > 0) {
        uapi_at_report(response);
    }
}

static int tp78_oled_at_task(void *arg)
{
    unused(arg);

    while (true) {
        tp78_oled_at_cmd_t command;
        uint32_t msg_size = sizeof(command);

        if (osal_msg_queue_read_copy(g_tp78_oled_at_queue, &command, &msg_size,
            OSAL_MSGQ_WAIT_FOREVER) != OSAL_SUCCESS) {
            continue;
        }

        g_tp78_oled_at_busy = true;
        tp78_oled_at_report(command, tp78_oled_at_execute(command) == 0 ? "OK" : "FAIL");
        g_tp78_oled_at_busy = false;
        osal_msleep(1);
    }

    return 0;
}

static at_ret_t tp78_oled_at_enqueue(tp78_oled_at_cmd_t command)
{
    if (!g_tp78_oled_at_ready) {
        tp78_oled_at_report(command, "NOTREADY");
        return AT_RET_CMD_PARA_ERROR;
    }

    if (g_tp78_oled_at_busy || osal_msg_queue_is_full(g_tp78_oled_at_queue)) {
        tp78_oled_at_report(command, "BUSY");
        return AT_RET_CMD_PARA_ERROR;
    }

    if (osal_msg_queue_write_copy(g_tp78_oled_at_queue, &command, sizeof(command),
        OSAL_MSGQ_NO_WAIT) != OSAL_SUCCESS) {
        tp78_oled_at_report(command, "QUEUEFAIL");
        return AT_RET_CMD_PARA_ERROR;
    }

    tp78_oled_at_report(command, "QUEUED");
    return AT_RET_OK;
}

static at_ret_t tp78_at_oled_test(void)
{
    return tp78_oled_at_enqueue(TP78_OLED_AT_CMD_SHOW);
}

static at_ret_t tp78_at_oled_on_test(void)
{
    return tp78_oled_at_enqueue(TP78_OLED_AT_CMD_ON);
}

static at_ret_t tp78_at_oled_data1_test(void)
{
    return tp78_oled_at_enqueue(TP78_OLED_AT_CMD_DATA1);
}

static at_ret_t tp78_at_oled_cursor_test(void)
{
    return tp78_oled_at_enqueue(TP78_OLED_AT_CMD_CURSOR);
}

static at_ret_t tp78_at_oled_data16_test(void)
{
    return tp78_oled_at_enqueue(TP78_OLED_AT_CMD_DATA16);
}

static const at_cmd_entry_t g_tp78_at_cmds[] = {
    {
        "TP78OLED",
        TP78_AT_CMD_ID_OLED,
        AT_FLAG_NONE,
        NULL,
        (at_cmd_func_t)tp78_at_oled_test,
        NULL,
        NULL,
        NULL,
    },
    {
        "TP78OLEDON",
        TP78_AT_CMD_ID_OLED_ON,
        AT_FLAG_NONE,
        NULL,
        (at_cmd_func_t)tp78_at_oled_on_test,
        NULL,
        NULL,
        NULL,
    },
    {
        "TP78OLED1",
        TP78_AT_CMD_ID_OLED_DATA1,
        AT_FLAG_NONE,
        NULL,
        (at_cmd_func_t)tp78_at_oled_data1_test,
        NULL,
        NULL,
        NULL,
    },
    {
        "TP78OLEDCUR",
        TP78_AT_CMD_ID_OLED_CURSOR,
        AT_FLAG_NONE,
        NULL,
        (at_cmd_func_t)tp78_at_oled_cursor_test,
        NULL,
        NULL,
        NULL,
    },
    {
        "TP78OLED16",
        TP78_AT_CMD_ID_OLED_DATA16,
        AT_FLAG_NONE,
        NULL,
        (at_cmd_func_t)tp78_at_oled_data16_test,
        NULL,
        NULL,
        NULL,
    },
};

void tp78_at_debug_init(void)
{
    osal_task *task = NULL;

    errcode_t ret = uapi_at_cmd_table_register(g_tp78_at_cmds,
        (uint32_t)TP78_ARRAY_SIZE(g_tp78_at_cmds), 0);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[tp78] AT debug register failed:0x%x\r\n", (unsigned int)ret);
        return;
    }

    if (osal_msg_queue_create("Tp78OledAtQ", TP78_OLED_AT_QUEUE_LEN, &g_tp78_oled_at_queue,
        0, sizeof(tp78_oled_at_cmd_t)) != OSAL_SUCCESS) {
        osal_printk("[tp78] OLED AT queue create failed\r\n");
        return;
    }

    osal_kthread_lock();
    task = osal_kthread_create(tp78_oled_at_task, NULL,
        "Tp78OledAtTask", TP78_OLED_AT_TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, TP78_OLED_AT_TASK_PRIORITY);
        g_tp78_oled_at_ready = true;
    } else {
        osal_printk("[tp78] OLED AT task creation failed\r\n");
    }
    osal_kthread_unlock();

    osal_printk("[tp78] AT debug ready: AT+TP78OLED/ON/CUR/1/16 async\r\n");
}
