$git = 'C:\Users\AMAN\AppData\Local\GitHubDesktop\app-3.6.3\resources\app\git\cmd\git.exe'
$repo = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom'
& $git -C $repo add -A
& $git -C $repo commit -m 'fix: gzip compress index.html (169KB->44KB), fix ESP32 page truncation on Node A and B'
& $git -C $repo push origin main
