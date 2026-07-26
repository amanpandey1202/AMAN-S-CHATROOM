$urls = @(
  "https://raw.githubusercontent.com/amanpandey1202/AMAN-S-CHATROOM/main/node_a/build/esp32.esp32.esp32/node_a.ino.bootloader.bin",
  "https://raw.githubusercontent.com/amanpandey1202/AMAN-S-CHATROOM/main/node_a/build/esp32.esp32.esp32/node_a.ino.partitions.bin",
  "https://raw.githubusercontent.com/amanpandey1202/AMAN-S-CHATROOM/main/node_a/build/esp32.esp32.esp32/boot_app0.bin",
  "https://raw.githubusercontent.com/amanpandey1202/AMAN-S-CHATROOM/main/node_a/build/esp32.esp32.esp32/node_a.ino.bin",
  "https://raw.githubusercontent.com/amanpandey1202/AMAN-S-CHATROOM/main/node_b/build/esp32.esp32.esp32/node_b.ino.bootloader.bin",
  "https://raw.githubusercontent.com/amanpandey1202/AMAN-S-CHATROOM/main/node_b/build/esp32.esp32.esp32/node_b.ino.partitions.bin",
  "https://raw.githubusercontent.com/amanpandey1202/AMAN-S-CHATROOM/main/node_b/build/esp32.esp32.esp32/boot_app0.bin",
  "https://raw.githubusercontent.com/amanpandey1202/AMAN-S-CHATROOM/main/node_b/build/esp32.esp32.esp32/node_b.ino.bin"
)

foreach ($url in $urls) {
  try {
    $res = Invoke-WebRequest -Uri $url -Method Head -UseBasicParsing
    $len = $res.Headers["Content-Length"]
    Write-Host "✅ OK ($len bytes): $url"
  } catch {
    Write-Host "❌ FAILED: $url"
  }
}
