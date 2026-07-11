idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES mruby_component esp_driver_gpio joltwallet__littlefs
)

# main/storage/ の中身を LittleFS イメージ(storage.bin)に固め、storage 区画へ焼く。
#   第1引数 storage = パーティション名 / 第2引数 storage = 固めるフォルダ(main/storage)
#   FLASH_IN_PROJECT = idf.py flash 時に storage.bin も一緒に書き込む
littlefs_create_partition_image(storage storage FLASH_IN_PROJECT)
