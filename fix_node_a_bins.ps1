$src = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom\node_b\build\esp32.esp32.esp32'
$dst = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom\node_a\build\esp32.esp32.esp32'

# Copy the board-generic binaries (identical for any ESP32 Arduino build with same partition scheme)
Copy-Item "$src\boot_app0.bin" "$dst\boot_app0.bin" -Force
Copy-Item "$src\node_b.ino.partitions.bin" "$dst\node_a.ino.partitions.bin" -Force

Write-Host "Copied boot_app0.bin -> node_a"
Write-Host "Copied partitions.bin -> node_a (renamed to node_a.ino.partitions.bin)"
Write-Host ""
Write-Host "=== node_a build dir now ==="
Get-ChildItem $dst | Select-Object Name, Length | Format-Table -AutoSize
