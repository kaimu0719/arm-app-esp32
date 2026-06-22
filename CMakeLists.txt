# --- プロジェクト最上位の CMakeLists.txt ---
# ESP-IDF プロジェクトの「入口」。idf.py は最初にこれを読む。
# 参照: https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-guides/build-system.html#mandatory-parts

# IDF が要求する最低 CMake バージョン
# 参照: https://cmake.org/cmake/help/latest/command/cmake_minimum_required.html
cmake_minimum_required(VERSION 3.16)

# ESP-IDF 本体のビルド機構（コンポーネント探索・ツールチェーン設定など）を取り込む。
# 参照: https://cmake.org/cmake/help/latest/command/include.html
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# プロジェクト名を宣言（生成されるバイナリ名 arm-app-esp32.elf などに使われる）。
# 参照: https://cmake.org/cmake/help/latest/command/project.html
project(arm-app-esp32)
