$git = 'C:\Users\AMAN\AppData\Local\GitHubDesktop\app-3.6.3\resources\app\git\cmd\git.exe'
$repo = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom'

& $git -C $repo add -A
& $git -C $repo commit -m 'fix: complete mesh radio rewrite (disable fragile auto-ack, add 3x burst redundancy, fixed 32-byte payloads, FIFO queue drain loop)'
& $git -C $repo push origin main
