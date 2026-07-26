$git = 'C:\Users\AMAN\AppData\Local\GitHubDesktop\app-3.6.3\resources\app\git\cmd\git.exe'
$repo = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom'

& $git -C $repo add -A
& $git -C $repo commit -m 'fix: implement large 10KB radio reassembly buffer and wire voice/mic packet forwarding over mesh radio link'
& $git -C $repo push origin main
