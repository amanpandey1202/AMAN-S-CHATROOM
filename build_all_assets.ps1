# PowerShell Script to compile all web assets into C++ headers for Node A & Node B
# Uses GZIP compression for index.html to dramatically reduce PROGMEM size and
# fix page truncation issues on ESP32 (169KB raw → ~28KB compressed).

Add-Type -AssemblyName System.IO.Compression

# ─── Helper: GZIP compress bytes → C++ uint8_t array header ─────────────────
function Make-GzipHeader {
    param([string]$inputFile, [string]$arrayName, [string]$lenName)

    $rawBytes = [System.IO.File]::ReadAllBytes($inputFile)

    # Compress into memory stream
    $ms = New-Object System.IO.MemoryStream
    $gz = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionLevel]::Optimal)
    $gz.Write($rawBytes, 0, $rawBytes.Length)
    $gz.Close()
    $compressed = $ms.ToArray()
    $ms.Close()

    $origKB   = [Math]::Round($rawBytes.Length / 1024, 1)
    $gzipKB   = [Math]::Round($compressed.Length / 1024, 1)
    $ratio    = [Math]::Round((1 - $compressed.Length / $rawBytes.Length) * 100, 0)
    Write-Host "  $inputFile : ${origKB} KB  →  ${gzipKB} KB gzip  (${ratio}% smaller)"

    # Build hex lines (16 bytes per line for readability)
    $lines = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $compressed.Length; $i += 16) {
        $chunk = $compressed[$i..[Math]::Min($i + 15, $compressed.Length - 1)]
        $hex   = ($chunk | ForEach-Object { "0x{0:X2}" -f $_ }) -join ", "
        $lines.Add("  $hex,")
    }
    # Fix trailing comma on last line
    $last = $lines[$lines.Count - 1].TrimEnd(',')
    $lines[$lines.Count - 1] = $last

    $body = $lines -join "`r`n"
    $out  = "const uint8_t ${arrayName}[] PROGMEM = {`r`n${body}`r`n};`r`n"
    $out += "const size_t  ${lenName} = sizeof(${arrayName});`r`n"
    return $out
}

# ─── Helper: plain text → C++ raw literal header (for small files) ───────────
function Make-RawHeader {
    param([string]$inputFile, [string]$arrayName, [string]$lenName)

    $text = [System.IO.File]::ReadAllText($inputFile, [System.Text.Encoding]::UTF8)
    $out  = "const char ${arrayName}[] PROGMEM = R`"rawliteral(`n" + $text + "`n)rawliteral`";`n"
    $out += "const unsigned int ${lenName} = sizeof(${arrayName}) - 1;`n"
    return $out
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

Write-Host ""
Write-Host "=== Building C++ asset headers ==="

# ─── index.html → GZIP byte array (large file, needs compression) ────────────
$html_h      = Make-GzipHeader "index.html"   "index_html_gz"   "index_html_gz_len"

# ─── manifest.json → raw literal (small file, no need to compress) ───────────
$manifest_h  = Make-RawHeader  "manifest.json" "manifest_json"  "manifest_json_len"

# ─── sw.js → raw literal (tiny file) ─────────────────────────────────────────
$sw_h        = Make-RawHeader  "sw.js"         "sw_js"          "sw_js_len"

# ─── Write to node_a ─────────────────────────────────────────────────────────
[System.IO.File]::WriteAllText("node_a/index_html.h",    $html_h,     $utf8NoBom)
[System.IO.File]::WriteAllText("node_a/manifest_json.h", $manifest_h, $utf8NoBom)
[System.IO.File]::WriteAllText("node_a/sw_js.h",         $sw_h,       $utf8NoBom)

# ─── Write to node_b ─────────────────────────────────────────────────────────
[System.IO.File]::WriteAllText("node_b/index_html.h",    $html_h,     $utf8NoBom)
[System.IO.File]::WriteAllText("node_b/manifest_json.h", $manifest_h, $utf8NoBom)
[System.IO.File]::WriteAllText("node_b/sw_js.h",         $sw_h,       $utf8NoBom)

Write-Host ""
Write-Host "=== IMPORTANT: Update .ino HTTP handler for GZIP ==="
Write-Host "  In node_a.ino / node_b.ino, change the '/' route to:"
Write-Host '  response->addHeader("Content-Encoding", "gzip");'
Write-Host '  req->send_P(200, "text/html", index_html_gz, index_html_gz_len);'
Write-Host ""
Write-Host "All assets compiled successfully!"
