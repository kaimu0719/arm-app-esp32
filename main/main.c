#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/string.h>

void app_main(void)
{
    // mruby VMを起動する
    mrb_state *mrb = mrb_open();
    if (mrb == NULL) {
        printf("mruby: mrb_open failed\n");
        return;
    }
    printf("mruby: VM started\n");

    int count = 0;
    while (1) {
        // Ruby のコードを文字列で渡して実行し、「戻り値」を受け取る。
        // #{7 * 6} は Ruby 側で計算される（= 42）。count は C 側の値。
        char code[96];
        snprintf(code, sizeof(code),
                 "\"hello from mruby! count=%d, 7*6=#{7 * 6}\"", count++);
        mrb_value result = mrb_load_string(mrb, code);

        if (mrb->exc) {
            // Ruby 実行中に例外が出た場合
            printf("mruby: exception raised\n");
            mrb->exc = NULL;                 // 例外をクリアして次へ
        } else {
            // 戻り値を to_s で文字列化 → C 文字列に変換 → C 側で表示
            mrb_value str = mrb_funcall(mrb, result, "to_s", 0);
            printf("Ruby => %s\n", mrb_str_to_cstr(mrb, str));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    mrb_close(mrb);
}
