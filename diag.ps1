$c = [IO.File]::ReadAllText('index.html')
$pos = $c.IndexOf(')rawliteral')
Write-Host "rawliteral conflict pos: $pos (if -1 = OK, if >= 0 = PROBLEM)"

$sz = (Get-Item 'index.html').Length
Write-Host "index.html size: $sz bytes ($([Math]::Round($sz/1024,1)) KB)"

$hz = (Get-Item 'node_a\index_html.h').Length
Write-Host "index_html.h size: $hz bytes ($([Math]::Round($hz/1024,1)) KB)"

# Check how many lines the header has vs the html
$htmlLines = ($c -split "`n").Count
Write-Host "index.html lines: $htmlLines"

# Check the end of the header file
$hc = [IO.File]::ReadAllText('node_a\index_html.h')
$tail = $hc.Substring([Math]::Max(0, $hc.Length - 150))
Write-Host "--- Last 150 chars of index_html.h ---"
Write-Host $tail

# Check the node_a.ino to see how it handles the HTML
Write-Host ""
Write-Host "--- node_a.ino size ---"
$ino = (Get-Item 'node_a\node_a.ino').Length
Write-Host "node_a.ino: $ino bytes ($([Math]::Round($ino/1024,1)) KB)"
