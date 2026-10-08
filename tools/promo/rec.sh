#!/bin/bash
# usage: rec.sh NAME SECONDS  -> clips/NAME.mp4 (Lumen window only) + clips/NAME.ram (t_sec ram_mb)
N=$1; S=$2; D=~/sites/promo/clips
P=$(pgrep -f 'build/lumen' | head -1)
read X Y W H < <(osascript -e 'tell application "System Events" to tell (first process whose name is "lumen") to set {p, s} to {position, size} of front window' -e 'return (item 1 of p as text) & " " & (item 2 of p as text) & " " & (item 1 of s as text) & " " & (item 2 of s as text)' | tr ',' ' ')
SC=$(python3 -c "import subprocess,re;o=subprocess.run(['system_profiler','SPDisplaysDataType'],capture_output=True,text=True).stdout;print(2 if 'Retina' in o else 1)")
ffmpeg -hide_banner -loglevel error -y -f avfoundation -capture_cursor 1 -framerate 30 -i "0:none" -t $S \
  -vf "crop=$((W*SC)):$((H*SC)):$((X*SC)):$((Y*SC)),scale=800:-2,fps=30" -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p $D/$N.mp4 &
F=$!; t0=$(python3 -c 'import time;print(time.time())'); : > $D/$N.ram
while kill -0 $F 2>/dev/null; do
  mb=$(footprint -p $P 2>/dev/null | awk '/phys_footprint:/{v=$2; if($3~/^G/)v*=1024; if($3~/^K/)v/=1024; print int(v)}')
  echo "$(python3 -c "import time;print(round(time.time()-$t0,2))") $mb" >> $D/$N.ram; sleep 0.5
done
echo "$N done $(wc -l < $D/$N.ram) samples"
