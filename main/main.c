#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// app_main() は ESP-IDF におけるアプリの入口。
// （C の main() ではない。IDF が起動処理のあとに app_main を呼ぶ）
void app_main(void)
{
    int count = 0;
    while (1) {
        printf("hello esp32 (count=%d)\n", count++);
        // FreeRTOS のタスク遅延。1000ms 待つ間、CPU を他に譲る。
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
