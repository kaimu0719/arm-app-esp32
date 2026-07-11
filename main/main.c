#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_littlefs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

#include <mruby.h>
#include <mruby/compile.h>

// ============================================================================
// Ruby から呼べる C 関数（GPIO / delay / log）
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
// WiFi (STA) — sdkconfig(menuconfig) の SSID/PASS で接続して IP を取得
//   WiFi は非同期(イベント駆動)。イベントグループで「接続完了/失敗」まで待つ。
// ============================================================================
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     6

static EventGroupHandle_t s_wifi_events;
static int  s_retry = 0;
static char s_ip[16];              // "255.255.255.255" + '\0'

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();                             // 起動したら接続開始
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < WIFI_MAX_RETRY) {                 // 切れたら数回リトライ
            esp_wifi_connect();
            s_retry++;
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip, sizeof(s_ip));  // IPを文字列化
        s_retry = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

// WiFi.connect() : 接続して IP 文字列を返す（失敗時は nil）
static mrb_value mrb_wifi_connect(mrb_state *mrb, mrb_value self)
{
    s_wifi_events = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    // sdkconfig(menuconfig)の値を設定に流し込む
    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid,     CONFIG_WIFI_SSID,     sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD, sizeof(wifi_config.sta.password));

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    // 「接続成功」か「失敗」のビットが立つまでブロックして待つ
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return mrb_str_new_cstr(mrb, s_ip);    // IP文字列を Ruby へ返す
    }
    return mrb_nil_value();                     // 失敗
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
    // 0) NVS 初期化（WiFi が校正データ保存に使う。WiFi 初期化の前に必須）
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

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

    // 3) Ruby に GPIO / delay / log / WiFi を公開
    struct RClass *gpio = mrb_define_module(mrb, "GPIO");
    mrb_define_module_function(mrb, gpio, "setup", mrb_gpio_setup, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, gpio, "write", mrb_gpio_write, MRB_ARGS_REQ(2));
    mrb_define_method(mrb, mrb->kernel_module, "delay", mrb_delay, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, mrb->kernel_module, "log",   mrb_log,   MRB_ARGS_REQ(1));

    struct RClass *wifi = mrb_define_module(mrb, "WiFi");
    mrb_define_module_function(mrb, wifi, "connect", mrb_wifi_connect, MRB_ARGS_NONE());

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
