param([Parameter(Mandatory=$true)][string]$LuaPath)
$endpoint = 'http://127.0.0.1:9315/mcp'
$headersPath = Join-Path $env:TEMP 'ns_headers.txt'
$init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"loop","version":"1.0"}}}'
& curl.exe -s -D $headersPath -o NUL --max-time 10 -X POST $endpoint -H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream' -d $init
$sidLine = Get-Content $headersPath | Where-Object { $_ -match '^mcp-session-id:' } | Select-Object -First 1
$sid = ($sidLine -split ':', 2)[1].Trim()
& curl.exe -s -o NUL --max-time 10 -X POST $endpoint -H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream' -H "MCP-Session-Id: $sid" -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
$lua = Get-Content -Raw $LuaPath
$request = @{jsonrpc='2.0';id=2;method='tools/call';params=@{name='execute_script';arguments=@{script=$lua}}} | ConvertTo-Json -Depth 10 -Compress
$requestPath = Join-Path $env:TEMP 'ns_request.json'
Set-Content -LiteralPath $requestPath -Value $request -Encoding utf8
$raw = & curl.exe -s --max-time 300 -X POST $endpoint -H 'Content-Type: application/json' -H 'Accept: application/json, text/event-stream' -H "MCP-Session-Id: $sid" --data-binary "@$requestPath"
$jsonText = ($raw -join "`n") -replace '^data: ', ''
$result = $jsonText | ConvertFrom-Json
$result.result.content | ForEach-Object { $_.text }
