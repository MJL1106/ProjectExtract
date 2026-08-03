#!/bin/bash
# vibe.sh <tool_name> <args_json_file>   — call a VibeUE MCP tool over raw curl (:8088)
WD="C:/Users/matth/AppData/Local/Temp/nsloop"
mkdir -p "$WD"
EP="http://127.0.0.1:8088/mcp"
H1="Content-Type: application/json"; H2="Accept: application/json, text/event-stream"
SID=$(curl -s -D "$WD/vhdr.txt" -o /dev/null --max-time 10 -X POST "$EP" -H "$H1" -H "$H2" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"loop","version":"1.0"}}}'; \
  grep -i '^mcp-session-id:' "$WD/vhdr.txt" | tr -d '\r' | sed 's/^[^:]*: *//')
curl -s -o /dev/null --max-time 10 -X POST "$EP" -H "$H1" -H "$H2" -H "MCP-Session-Id: $SID" \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
if [ "$1" = "--list" ]; then
  curl -s --max-time 60 -X POST "$EP" -H "$H1" -H "$H2" -H "MCP-Session-Id: $SID" \
    -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | sed 's/^data: //'
  exit 0
fi
python -c "import json,sys;args=json.load(open(r'$2',encoding='utf-8'));print(json.dumps({'jsonrpc':'2.0','id':2,'method':'tools/call','params':{'name':'$1','arguments':args}}))" > "$WD/vreq.json"
curl -s --max-time 300 -X POST "$EP" -H "$H1" -H "$H2" -H "MCP-Session-Id: $SID" --data-binary @"$WD/vreq.json" \
  | sed -e 's/^data: //' -e '/^id: [0-9]*$/d' \
  | python -c "import json,sys
raw=sys.stdin.read().strip()
try:
    d=json.loads(raw)
    if 'result' in d:
        for c in d['result'].get('content',[]): print(c.get('text',''))
    else: print('JSONRPC-ERROR:', json.dumps(d)[:3000])
except Exception as e: print('RAW:', raw[:5000])"
