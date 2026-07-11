#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"      // GPIO ドライバ
#include "esp_littlefs.h"     // littlefs マウント

#include <mruby.h>           // mruby 本体
#include <mruby/compile.h>   // mrb_load_string

// ============================================================================
// Ruby から呼べる C 関数
// ============================================================================

// GPIO.setup(pin) : ピンを出力モードに
static mrb_value mrb_gpio_setup(mrb_state *mrb, mrb_value self)
{
    mrb_int pin;
    mrb_get_args(mrb, "i", &pin);
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    return mrb_nil_value();
}

// GPIO.write(pin, level) : ピンに 0/1 を出力
static mrb_value mrb_gpio_write(mrb_state *mrb, mrb_value self)
{
    mrb_int pin, level;
    mrb_get_args(mrb, "ii", &pin, &level);
    gpio_set_level((gpio_num_t)pin, (uint32_t)level);
    return mrb_nil_value();
}

// delay(ms) : 指定ミリ秒だけ待つ（グローバル関数）
static mrb_value mrb_delay(mrb_state *mrb, mrb_value self)
{
    mrb_int ms;
    mrb_get_args(mrb, "i", &ms);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return mrb_nil_value();
}

// log(str) : シリアルに1行出力（mruby 4.0 は puts 無しなので自前で用意）
static mrb_value mrb_log(mrb_state *mrb, mrb_value self)
{
    const char *s;
    mrb_get_args(mrb, "z", &s);        // "z" = NUL終端のC文字列
    printf("%s\n", s);
    return mrb_nil_value();
}

// ============================================================================
// littlefs (storage 区画) を /littlefs にマウント
// ============================================================================
static esp_err_t mount_storage(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",         // マウント先パス
        .partition_label = "storage",     // partitions.csv の区画名
        .format_if_mount_failed = false,  // 失敗時に自動フォーマットはしない
    };
    return esp_vfs_littlefs_register(&conf);
}

// ファイルを丸ごと読み、malloc したバッファ(NUL終端)を返す。失敗時 NULL。
static char *read_whole_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    return buf;
}

void app_main(void)
{
    // 1) storage 区画を littlefs でマウント
    if (mount_storage() != ESP_OK) {
        printf("littlefs: mount failed\n");
        return;
    }
    printf("littlefs: mounted at /littlefs\n");

    // 2) mruby VM を起動
    mrb_state *mrb = mrb_open();
    if (mrb == NULL) {
        printf("mruby: mrb_open failed\n");
        return;
    }
    printf("mruby: VM started\n");

    // 3) Ruby に GPIO モジュールと delay/log を公開
    struct RClass *gpio = mrb_define_module(mrb, "GPIO");
    mrb_define_module_function(mrb, gpio, "setup", mrb_gpio_setup, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, gpio, "write", mrb_gpio_write, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, mrb->kernel_module, "delay", mrb_delay, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, mrb->kernel_module, "log",   mrb_log,   MRB_ARGS_REQ(1));

    // 4) /littlefs/main.rb を読み込んで実行（main.rb 側に無限ループがある）
    char *code = read_whole_file("/littlefs/main.rb");
    if (code == NULL) {
        printf("littlefs: cannot read /littlefs/main.rb\n");
        mrb_close(mrb);
        return;
    }
    printf("running /littlefs/main.rb ...\n");
    mrb_load_string(mrb, code);

    if (mrb->exc) {
        printf("mruby: exception raised in main.rb\n");
    }
    free(code);
    mrb_close(mrb);
}
