#!/bin/bash
WD="C:/Users/matth/AppData/Local/Temp/nsloop"
EP="http://127.0.0.1:9315/mcp"
H1="Content-Type: application/json"; H2="Accept: application/json, text/event-stream"
SID=$(curl -s -D "$WD/hdr.txt" -o /dev/null --max-time 10 -X POST "$EP" -H "$H1" -H "$H2" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"loop","version":"1.0"}}}'; \
  grep -i mcp-session-id "$WD/hdr.txt" | tr -d '\r' | sed 's/.*: //')
curl -s -o /dev/null --max-time 10 -X POST "$EP" -H "$H1" -H "$H2" -H "MCP-Session-Id: $SID" \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
python -c "import json;lua=open(r'$1',encoding='utf-8').read();print(json.dumps({'jsonrpc':'2.0','id':2,'method':'tools/call','params':{'name':'execute_script','arguments':{'script':lua}}}))" > "$WD/req.json"
curl -s --max-time 300 -X POST "$EP" -H "$H1" -H "$H2" -H "MCP-Session-Id: $SID" --data-binary @"$WD/req.json" \
  | sed 's/^data: //' \
  | python -c "import json,sys
raw=sys.stdin.read().strip()
try:
    d=json.loads(raw)
    if 'result' in d:
        for c in d['result'].get('content',[]): print(c.get('text',''))
    else: print('JSONRPC-ERROR:', json.dumps(d)[:2000])
except Exception as e: print('RAW:', raw[:3000])"
