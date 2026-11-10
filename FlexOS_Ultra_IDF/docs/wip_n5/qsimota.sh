#!/bin/bash
cd /tmp/claude-0/sp/wt/FlexOS_Ultra_IDF
out=$(cmake --build /tmp/claude-0/sp/simwt -j3 --target flexos_ui_sim 2>&1)
if echo "$out" | grep -q "error"; then echo "error de compilacion"; exit 0; fi
res=$(timeout 120 /tmp/claude-0/sp/simwt/flexos_ui_sim /tmp/claude-0/sp/simout ota 2>&1)
if ! echo "$res" | grep -q "escenas con fallos"; then echo "CRASH o bucle"; exit 0; fi
n=$(echo "$res" | grep -c "FALLO linea")
echo "checks=1 fails=$n"
