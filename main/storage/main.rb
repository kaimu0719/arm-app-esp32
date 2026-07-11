# main.rb — ESP32 上で実行される Ruby スクリプト
# C 側(main.c)が公開する関数: GPIO.setup/write, delay, log, WiFi.connect

# --- WiFi 接続（SSID/PASS は menuconfig で設定済み → sdkconfig 参照）---
log("connecting WiFi...")
ip = WiFi.connect
if ip
  log("WiFi connected!  IP = #{ip}")
else
  log("WiFi failed")
end

# --- 以後は L チカ ---
LED = 2
GPIO.setup(LED)

count = 0
loop do
  GPIO.write(LED, count % 2)
  log("blink #{count}")
  count += 1
  delay(1000)
end
