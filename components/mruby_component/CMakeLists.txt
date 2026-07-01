# ============================================================================
# mruby_component … mruby を IDF コンポーネントとして組み込む「糊」
#
#   役割は3つ:
#     1. rake を呼んで libmruby.a（ESP32用）をビルドする
#     2. その .a を IDF に「ビルド済みライブラリ」として登録する
#     3. mruby/include を公開し、main から #include <mruby.h> できるようにする
# ============================================================================

set(MRUBY_DIR     "${COMPONENT_DIR}/mruby")                       # mruby本体(submodule)
set(MRUBY_CONFIG  "${COMPONENT_DIR}/esp32_build_config.rb")       # 先に書いたビルド設定
set(LIBMRUBY_FILE "${MRUBY_DIR}/build/esp32/lib/libmruby.a")      # 生成物(cross版)

# mruby は rake ビルド時に一部のヘッダ(presym/id.h 等)を build/esp32/include に
# “生成”する。fullclean 直後はまだ存在しないので、configure 時の存在チェックを
# 通すために空のディレクトリだけ先に用意しておく（中身は rake が後で埋める）。
file(MAKE_DIRECTORY "${MRUBY_DIR}/build/esp32/include")

# このコンポーネントを登録。ソースは持たず、ヘッダの棚を2つ公開する:
#   mruby/include             … 静的ヘッダ(mruby.h など)
#   mruby/build/esp32/include … rake が生成するヘッダ(mruby/presym/id.h など)
idf_component_register(
  INCLUDE_DIRS "mruby/include" "mruby/build/esp32/include"
)

# --- libmruby.a を rake でビルドするカスタムコマンド -------------------------
# 前回ログで見た「rake 起動コマンド」の正体がこれ。env で必要な情報を渡す:
#   MRUBY_CONFIG        … どのビルド設定を使うか
#   CC / LD / AR        … cross(ESP32)用の xtensa ツール
#                         （host側は build_config が絶対パスで上書きするので安全）
#   COMPONENT_INCLUDES  … このコンポーネントから見える IDF の全インクルードパス
#                         （freertos 等のヘッダを mruby のビルドに渡す）
add_custom_command(
  OUTPUT  ${LIBMRUBY_FILE}
  COMMAND ${CMAKE_COMMAND} -E env
            "MRUBY_CONFIG=${MRUBY_CONFIG}"
            "CC=${CMAKE_C_COMPILER}"
            "LD=${CMAKE_LINKER}"
            "AR=${CMAKE_AR}"
            "COMPONENT_INCLUDES=$<TARGET_PROPERTY:${COMPONENT_TARGET},INTERFACE_INCLUDE_DIRECTORIES>"
            rake
  WORKING_DIRECTORY ${MRUBY_DIR}
  # 設定ファイルが変わったら libmruby.a を作り直す（依存として登録）。
  # ※ ただし mruby の rake はコンパイルフラグ変更だけでは .o を再コンパイル
  #    しないことがあるため、設定を変えた直後は mruby/build を手で消すのが確実。
  DEPENDS ${MRUBY_CONFIG}
  BYPRODUCTS ${MRUBY_CONFIG}.lock
  VERBATIM
)

# 生成した .a を CMake のライブラリターゲットとして扱えるようにする。
# PRIV_REQUIRES esp_rom:
#   mruby は例外処理に setjmp/longjmp を使う。IDF は longjmp を --wrap=longjmp で
#   差し替え、その実体 __wrap_longjmp を esp_rom が提供する。libmruby が esp_rom に
#   依存すると宣言することで、リンク順が解決され __wrap_longjmp が見つかるようになる。
add_prebuilt_library(libmruby ${LIBMRUBY_FILE} PRIV_REQUIRES esp_rom)

# このコンポーネントを使う側(main)に libmruby.a がリンクされるようにする
target_link_libraries(${COMPONENT_LIB} INTERFACE libmruby)

# rake ビルドを "mruby" ターゲットとしてまとめ、コンポーネントの前提に紐付ける
# （= mruby_component をビルトする前に必ず libmruby.a が作られる）
add_custom_target(mruby DEPENDS ${LIBMRUBY_FILE})
add_dependencies(${COMPONENT_LIB} mruby)
