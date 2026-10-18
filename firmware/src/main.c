/*
 * Vibration-monitoring node: FreeRTOS tasks around the portable application in app/.
 *
 *   modbus      waits for bytes, ends a frame after a silent gap, answers it    priority 3
 *   analysis    acquires a frame, computes features, classifies, every interval priority 2
 *   supervisor  feeds the watchdog only while both tasks show progress          priority 1
 *
 * The node state is shared under one mutex; the Modbus handler holds it for one frame.
 */
#include <stdbool.h>

#include "FreeRTOS.h"
#include "board.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"
#include "vmn/fft.h"
#include "vmn/node.h"
#include "vmn/prng.h"

#define MODBUS_BAUD 115200u
/* Modbus RTU ends a frame after 3.5 character times; above 19200 baud the specification fixes
 * this at 1.75 ms. With a 1 ms tick, a 3-tick timeout always covers it. */
#define FRAME_GAP_TICKS pdMS_TO_TICKS(3)
#define IDLE_POLL_TICKS pdMS_TO_TICKS(500)
#define MODBUS_DEADLINE_TICKS pdMS_TO_TICKS(2000)

static vmn_node_t s_node;
static vmn_modbus_t s_modbus;
static vmn_workspace_t s_workspace;
static SemaphoreHandle_t s_lock;
static StreamBufferHandle_t s_rx;
static volatile TickType_t s_modbus_beat;
static volatile TickType_t s_analysis_beat;

void vApplicationStackOverflowHook(TaskHandle_t task, char *name);
void vApplicationMallocFailedHook(void);

static void modbus_task(void *arg)
{
    (void)arg;
    static uint8_t req[VMN_MODBUS_MAX_ADU];
    static uint8_t resp[VMN_MODBUS_MAX_ADU];
    board_uart_init(MODBUS_BAUD, s_rx);

    for (;;) {
        s_modbus_beat = xTaskGetTickCount();
        if (xStreamBufferReceive(s_rx, req, 1, IDLE_POLL_TICKS) == 0u) {
            continue;
        }
        size_t len = 1;
        bool overflow = false;
        uint8_t byte;
        while (xStreamBufferReceive(s_rx, &byte, 1, FRAME_GAP_TICKS) == 1u) {
            if (len < sizeof req) {
                req[len++] = byte;
            } else {
                overflow = true;
            }
        }
        if (overflow) {
            continue; /* longer than any valid frame */
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_node.uart_overruns = board_uart_overruns();
        const size_t resp_len = vmn_modbus_handle(&s_modbus, req, len, resp, sizeof resp);
        const bool readdress = s_node.address_changed;
        const uint8_t new_address = (uint8_t)s_node.settings.slave_address;
        s_node.address_changed = false;
        xSemaphoreGive(s_lock);

        if (resp_len > 0u) {
            board_uart_write(resp, resp_len);
        }
        if (readdress) {
            s_modbus.address = new_address; /* the reply above still used the old address */
        }
    }
}

static void analysis_task(void *arg)
{
    (void)arg;
    uint32_t rng = vmn_prng_seed(0x5EED1234u);
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        const vmn_settings_t settings = s_node.settings;
        xSemaphoreGive(s_lock);

        vmn_features_t features;
        vmn_prediction_t prediction;
        if (vmn_node_analyse(&settings, &rng, &s_workspace, &features, &prediction) == 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            vmn_node_store_results(&s_node, &features, &prediction);
            xSemaphoreGive(s_lock);
        }
        s_analysis_beat = xTaskGetTickCount();
        (void)xTaskDelayUntil(&last, pdMS_TO_TICKS(settings.interval_ms));
    }
}

static void supervisor_task(void *arg)
{
    (void)arg;
    board_watchdog_start();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        const TickType_t now = xTaskGetTickCount();
        const TickType_t analysis_deadline = pdMS_TO_TICKS(2u * (uint32_t)s_node.settings.interval_ms + 2000u);
        const bool modbus_alive = (TickType_t)(now - s_modbus_beat) < MODBUS_DEADLINE_TICKS;
        const bool analysis_alive = (TickType_t)(now - s_analysis_beat) < analysis_deadline;
        if (modbus_alive && analysis_alive) {
            board_watchdog_kick();
        }
    }
}

int main(void)
{
    board_init();
    vmn_fft_init();
    vmn_node_init(&s_node);
    const vmn_modbus_map_t map = vmn_node_modbus_map(&s_node);
    vmn_modbus_init(&s_modbus, (uint8_t)s_node.settings.slave_address, &map);
    s_node.modbus_stats = &s_modbus.stats;

    s_lock = xSemaphoreCreateMutex();
    s_rx = xStreamBufferCreate(2u * VMN_MODBUS_MAX_ADU, 1);
    configASSERT(s_lock != NULL && s_rx != NULL);

    s_modbus_beat = s_analysis_beat = xTaskGetTickCount();
    configASSERT(xTaskCreate(modbus_task, "modbus", 512, NULL, 3, NULL) == pdPASS);
    configASSERT(xTaskCreate(analysis_task, "analysis", 1024, NULL, 2, NULL) == pdPASS);
    configASSERT(xTaskCreate(supervisor_task, "supervisor", 256, NULL, 1, NULL) == pdPASS);
    vTaskStartScheduler();
    for (;;) {
        /* only reached if the scheduler could not start */
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    (void)name;
    taskDISABLE_INTERRUPTS();
    for (;;) {
        /* the watchdog resets the node */
    }
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;) {
        /* the watchdog resets the node */
    }
}
