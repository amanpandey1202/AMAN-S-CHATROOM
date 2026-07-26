$git = 'C:\Users\AMAN\AppData\Local\GitHubDesktop\app-3.6.3\resources\app\git\cmd\git.exe'
$repo = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom'

& $git -C $repo add -A
& $git -C $repo commit -m 'fix: restore dotTerrain canvas & JS engine in index.html; optimize nRF24 mesh power/pipe reliability in node_a and node_b'
& $git -C $repo push origin main
