# main.rb — ESP32 上で実行される Ruby スクリプト
# GPIO / delay / log は C 側(main.c)が公開する関数（サブステップ4で登録）。
# このファイルは littlefs イメージに固められ storage 区画へ焼かれ、
# 起動時に mruby が読み込んで実行する。

LED = 2
GPIO.setup(LED)

count = 0
loop do
  GPIO.write(LED, count % 2)
  log("blink from main.rb: count=#{count}")
  count += 1
  delay(1000)
end
