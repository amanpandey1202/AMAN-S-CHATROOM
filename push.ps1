$git = 'C:\Users\AMAN\AppData\Local\GitHubDesktop\app-3.6.3\resources\app\git\cmd\git.exe'
$repo = 'c:\Users\AMAN\.gemini\antigravity\scratch\purple_chatroom'
& $git -C $repo add -A
& $git -C $repo commit -m 'fix: add missing node_a boot_app0.bin and partitions.bin so flasher can load all 4 binaries'
& $git -C $repo push origin main
