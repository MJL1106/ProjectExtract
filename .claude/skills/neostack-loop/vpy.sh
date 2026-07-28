#!/bin/bash
# vpy.sh <python_file>  — run an editor Python file through VibeUE execute_python_code
WD="C:/Users/matth/AppData/Local/Temp/nsloop"; mkdir -p "$WD"
python -c "import json,sys;json.dump({'code':open(r'$1',encoding='utf-8').read()},open(r'$WD/vargs.json','w'))"
bash "$(dirname "$0")/vibe.sh" execute_python_code "$WD/vargs.json"
