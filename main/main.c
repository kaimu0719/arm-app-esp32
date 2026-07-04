#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"      // ESP-IDF の GPIO ドライバ（gpio_set_level 等）

#include <mruby.h>           // mrb_open / mrb_close / mrb_define_module ...
#include <mruby/compile.h>   // mrb_load_string（Rubyソース文字列を実行）

// ============================================================================
// Ruby から呼べる C 関数（GPIO モジュールのメソッドの実体）
//   mruby のメソッドは「mrb_state* と self を受け取り mrb_value を返す」形。
//   Ruby 側の引数は mrb_get_args で C の変数に取り出す。
// ============================================================================

// GPIO.setup(pin) : ピンを出力モードに設定する
static mrb_value mrb_gpio_setup(mrb_state *mrb, mrb_value self)
{
    mrb_int pin;
    mrb_get_args(mrb, "i", &pin);                 // "i" = 整数を1つ受け取る
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    return mrb_nil_value();
}

// GPIO.write(pin, level) : ピンに 0/1 を出力する
static mrb_value mrb_gpio_write(mrb_state *mrb, mrb_value self)
{
    mrb_int pin, level;
    mrb_get_args(mrb, "ii", &pin, &level);        // "ii" = 整数を2つ
    gpio_set_level((gpio_num_t)pin, (uint32_t)level);
    return mrb_nil_value();
}

void app_main(void)
{
    // 1) mruby VM を起動
    mrb_state *mrb = mrb_open();
    if (mrb == NULL) {
        printf("mruby: mrb_open failed\n");
        return;
    }
    printf("mruby: VM started\n");

    // 2) Ruby に GPIO モジュールを定義し、上の C 関数をメソッドとして登録する。
    //    これで Ruby から GPIO.setup / GPIO.write が呼べるようになる。
    struct RClass *gpio = mrb_define_module(mrb, "GPIO");
    mrb_define_module_function(mrb, gpio, "setup", mrb_gpio_setup, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, gpio, "write", mrb_gpio_write, MRB_ARGS_REQ(2));

    // 3) ピンの初期化（Ruby から呼ぶ）
    mrb_load_string(mrb, "GPIO.setup(2)");

    // 4) 0.5秒ごとに Ruby から LED を on/off（count の偶奇で切替）
    int count = 0;
    while (1) {
        int level = count % 2;
        char code[64];
        snprintf(code, sizeof(code), "GPIO.write(2, %d)", level);
        mrb_load_string(mrb, code);               // ← Ruby が C の gpio_set_level を呼ぶ

        printf("Ruby: GPIO.write(2, %d)  (count=%d)\n", level, count);
        count++;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    mrb_close(mrb);
}
