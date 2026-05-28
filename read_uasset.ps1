param([string]$Path, [string]$Pattern)
$bytes = [System.IO.File]::ReadAllBytes($Path)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$tokens = $text -split '[^\x20-\x7E]+'
$tokens | Where-Object { $_.Length -gt 4 } | Select-String -Pattern $Pattern | Select-Object -First 100
