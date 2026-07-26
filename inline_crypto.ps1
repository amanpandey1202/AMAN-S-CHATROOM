$url = 'https://cdnjs.cloudflare.com/ajax/libs/crypto-js/4.1.1/crypto-js.min.js'
$cj = (Invoke-WebRequest -Uri $url -UseBasicParsing).Content
Write-Host "Downloaded CryptoJS: $($cj.Length) bytes"

$flasherPath = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom\flasher.html'
$html = [System.IO.File]::ReadAllText($flasherPath)

# Replace external script tag with inline script
$oldTag = '<script src="https://cdnjs.cloudflare.com/ajax/libs/crypto-js/4.1.1/crypto-js.min.js"></script>'
$newTag = "<script>`n" + $cj + "`n</script>"

if ($html.Contains($oldTag)) {
    $html = $html.Replace($oldTag, $newTag)
    [System.IO.File]::WriteAllText($flasherPath, $html)
    Write-Host "Successfully inlined CryptoJS into flasher.html!"
} else {
    Write-Host "Old tag not found in flasher.html"
}
