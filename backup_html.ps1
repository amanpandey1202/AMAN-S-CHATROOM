$src = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom'
$dst = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom\html_backup'

if (!(Test-Path $dst)) {
    New-Item -ItemType Directory -Path $dst | Out-Null
}

Copy-Item "$src\index.html" "$dst\index.html" -Force
Copy-Item "$src\preview.html" "$dst\preview.html" -Force

Write-Host "Successfully backed up index.html and preview.html to html_backup folder!"
