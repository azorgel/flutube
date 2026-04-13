#!/bin/bash
YTDLP=~/Library/Audio/Plug-Ins/Components/FluTube.component/Contents/Resources/yt-dlp
"$YTDLP" -x --audio-format wav --audio-quality 0 --no-playlist --no-part --ffmpeg-location /opt/homebrew/bin/ffmpeg -o '/tmp/flutube_test.%(ext)s' 'https://www.youtube.com/watch?v=dQw4w9WgXcQ'
echo "Exit code: $?"
ls -lh /tmp/flutube_test.* 2>/dev/null || echo "No output file found"
