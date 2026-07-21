# PowerShell Script to compile all web assets into C++ headers for Node A & Node B

# 1. Read files
$html = [System.IO.File]::ReadAllText("index.html", [System.Text.Encoding]::UTF8)
$manifest = [System.IO.File]::ReadAllText("manifest.json", [System.Text.Encoding]::UTF8)
$sw = [System.IO.File]::ReadAllText("sw.js", [System.Text.Encoding]::UTF8)

# 2. Format as C++ raw literal headers with length constants
$html_h = "const char index_html[] PROGMEM = R`"rawliteral(`n" + $html + "`n)rawliteral`";`nconst unsigned int index_html_len = sizeof(index_html) - 1;`n"
$manifest_h = "const char manifest_json[] PROGMEM = R`"rawliteral(`n" + $manifest + "`n)rawliteral`";`nconst unsigned int manifest_json_len = sizeof(manifest_json) - 1;`n"
$sw_h = "const char sw_js[] PROGMEM = R`"rawliteral(`n" + $sw + "`n)rawliteral`";`nconst unsigned int sw_js_len = sizeof(sw_js) - 1;`n"

# Create UTF-8 encoding without BOM
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

# 3. Write to node_a headers
[System.IO.File]::WriteAllText("node_a/index_html.h", $html_h, $utf8NoBom)
[System.IO.File]::WriteAllText("node_a/manifest_json.h", $manifest_h, $utf8NoBom)
[System.IO.File]::WriteAllText("node_a/sw_js.h", $sw_h, $utf8NoBom)

# 4. Write to node_b headers
[System.IO.File]::WriteAllText("node_b/index_html.h", $html_h, $utf8NoBom)
[System.IO.File]::WriteAllText("node_b/manifest_json.h", $manifest_h, $utf8NoBom)
[System.IO.File]::WriteAllText("node_b/sw_js.h", $sw_h, $utf8NoBom)

Write-Host "All assets (HTML, Manifest, Service Worker) compiled into C++ headers successfully without BOM!"
