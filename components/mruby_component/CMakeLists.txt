# ============================================================================
# mruby_component … mruby を IDF コンポーネントとして組み込む「糊」
#
#   役割は3つ:
#     1. rake を呼んで libmruby.a（ESP32用）をビルドする
#     2. その .a を IDF に「ビルド済みライブラリ」として登録する
#     3. mruby/include を公開し、main から #include <mruby.h> できるようにする
# ============================================================================

# --- COMPONENT_DIR について（混同注意）---------------------------------------
# COMPONENT_DIR は IDF が用意する変数で「このコンポーネント“自身”のフォルダ」の絶対パス。
#   ここでは .../arm-app-esp32/components/mruby_component を指す。
#   コンポーネントごとに値が違う（main の中なら .../main を指す）。
#
# ★「コンポーネントを探す“検索パス”」とは別物なので混同しないこと:
#     検索パス   = 部品を探しに行く“親フォルダ”一覧
#                  （IDF_PATH/components, PROJECT_DIR/components, main, managed_components …）
#     COMPONENT_DIR = 見つかった各部品“自身”が置かれている番地
# つまり ${COMPONENT_DIR}/mruby = components/mruby_component/mruby（= mruby本体submodule）。
# -----------------------------------------------------------------------------
set(MRUBY_DIR     "${COMPONENT_DIR}/mruby")                       # mruby本体(submodule)
set(MRUBY_CONFIG  "${COMPONENT_DIR}/esp32_build_config.rb")       # mrubyのビルド設定
set(LIBMRUBY_FILE "${MRUBY_DIR}/build/esp32/lib/libmruby.a")      # 生成物(ESP32向けlib)

# mruby は rake ビルド時に一部のヘッダ(presym/id.h 等)を build/esp32/include に
# 生成する。ところが下の idf_component_register は、INCLUDE_DIRS の各パスが
# 実在するディレクトリかを configure 時に検証する（IDF本体:
#   esp-idf/tools/cmake/component.cmake の IS_DIRECTORY→FATAL_ERROR）。
# rake が走るのはその後(ビルド時)なので、mruby がまだ一度もビルドされていない時
# （初回ビルド／mruby/build を手で削除した後 等）は configure 時点で存在せず、
# 検証に引っかかって止まる。→ 空のディレクトリだけ先に作って検証を通す
# （中身の実ヘッダは rake が後で埋める）。
# ※ idf.py fullclean が消すのは IDF の build/ だけで、mruby/build は消えない点に注意。
file(MAKE_DIRECTORY "${MRUBY_DIR}/build/esp32/include")

# このコンポーネントを登録。ソースは持たず、ヘッダの棚を2つ公開する:
#   mruby/include             … 静的ヘッダ(mruby.h など)
#   mruby/build/esp32/include … rake が生成するヘッダ(mruby/presym/id.h など)
# ※ INCLUDE_DIRS の相対パスは COMPONENT_DIR 基準で解決される。
#   例: "mruby/include" → ${COMPONENT_DIR}/mruby/include
idf_component_register(
  INCLUDE_DIRS "mruby/include" "mruby/build/esp32/include"
)

# --- libmruby.a を rake でビルドするカスタムコマンド -------------------------
# env で必要な情報を渡す:
#   MRUBY_CONFIG        … どのビルド設定を使うか
#   CC / LD / AR        … cross(ESP32)用の xtensa ツール
#                         （host側は build_config が絶対パスで上書きするので安全）
#   COMPONENT_INCLUDES  … このコンポーネントから見える IDF の全インクルードパス
#                         （freertos 等のヘッダを mruby のビルドに渡す）
# 参照: https://cmake.org/cmake/help/latest/command/add_custom_command.html#command:add_custom_command
# このadd_custom_commandの中では、rakeは実行されておらずあくまで「libmruby.aを作るにはこうします」とbuild.ninjaにルールを書くだけである。
# 実際にrakeが走るのはNinjaビルド中にlibmruby.aが必要になった時だけ。
add_custom_command(
  # libmruby.aをLIBMRUBY_FILEで指定している場所に生成する
  OUTPUT  ${LIBMRUBY_FILE}

  # 環境変数をセットしてrakeを起動させる。
  # -E env "KEY=VALUE" ... cmd = 「環境変数をセットしてから cmd を実行する」CMake 標準の移植性ある方法（OS を問わず動く）。
  COMMAND ${CMAKE_COMMAND} -E env
            "MRUBY_CONFIG=${MRUBY_CONFIG}"
            # cross(ESP32)用の xtensa ツール
            "CC=${CMAKE_C_COMPILER}"
            "LD=${CMAKE_LINKER}"
            "AR=${CMAKE_AR}"
            # ビルド時に「このコンポーネントから見える IDF の全ヘッダパス（freertos 等）」に展開される
            "COMPONENT_INCLUDES=$<TARGET_PROPERTY:${COMPONENT_TARGET},INTERFACE_INCLUDE_DIRECTORIES>"
            rake

  # rakeをRakefileがある場所で実行させる
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
# 参考: https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-guides/build-system.html#using-prebuilt-libraries-with-components
add_prebuilt_library(libmruby ${LIBMRUBY_FILE} PRIV_REQUIRES esp_rom)

# このコンポーネントを使う側(main)に libmruby.a がリンクされるようにする
# 参考: https://cmake.org/cmake/help/latest/command/target_link_libraries.html#id3
target_link_libraries(${COMPONENT_LIB} INTERFACE libmruby)

# rake ビルドを "mruby" ターゲットとしてまとめ、コンポーネントの前提に紐付ける
# （= mruby_component をビルトする前に必ず libmruby.a が作られる）
# 参考: https://cmake.org/cmake/help/latest/command/add_custom_target.html
# mrubyという名前のターゲットを作り、それはlibmruby.aに依存する
# mrubyターゲットをビルドする→libmruby.aが必要→add_custom_commandが走る
add_custom_target(mruby DEPENDS ${LIBMRUBY_FILE})

# 参考: https://cmake.org/cmake/help/latest/command/add_dependencies.html
# mruby_componentのライブラリをmrubyターゲットに依存させる
# → mruby_componentをビルドする前に、必ずmrubyターゲットを済ませるという順序保証
add_dependencies(${COMPONENT_LIB} mruby)
