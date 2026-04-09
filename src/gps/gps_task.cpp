#include "gps/gps_internal.h"

#include "pins.h"

#include <esp_task_wdt.h>
#include <esp_timer.h>

static void IRAM_ATTR pps_isr() {
    portENTER_CRITICAL_ISR(&pps_mux);
    pps_sync_us = esp_timer_get_time();
    portEXIT_CRITICAL_ISR(&pps_mux);
}

static void pps_init() {
    pinMode(PIN_GPS_PPS, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), pps_isr, RISING);
}

void gps_task(void* param) {
    (void)param;

    esp_task_wdt_add(nullptr);

    while (true) {
        int avail = Serial2.available();
        if (avail > 0) {
            for (int i = 0; i < avail; i++) {
                int c = Serial2.read();
                if (c >= 0) {
                    gps_feed_char(static_cast<char>(c));
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        esp_task_wdt_reset();
    }
}

void gps_init(QueueHandle_t gps_queue) {
    s_gps_queue = gps_queue;

    gps_uart_init();
    pps_init();

    s_fix_idx = 0;

    xTaskCreatePinnedToCore(
        gps_task,
        "task_gps",
        4096,
        nullptr,
        22,
        nullptr,
        0
    );
}
