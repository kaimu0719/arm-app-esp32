# --- "main" コンポーネントの定義 ---

idf_component_register(
  SRCS "main.c"        # このコンポーネントがコンパイルする .c
  INCLUDE_DIRS "."     # ヘッダ探索パス（このフォルダ自身）
)
