#!/bin/bash

# Variables
CAMERA_INPUT="/dev/video0"        # Adjust if your camera device is different
RTSP_SERVER_BINARY="rtsp-simple-server"
RTSP_SERVER_CONFIG="rtsp-simple-server.yml"
RTSP_STREAM_URL="rtsp://localhost:8554/live"
HLS_OUTPUT_DIR="./hls"
HLS_PLAYLIST="index.m3u8"
HTTP_SERVER_PORT=8080

# Create HLS output directory
mkdir -p $HLS_OUTPUT_DIR

# Create RTSP server configuration file
cat > $RTSP_SERVER_CONFIG << EOF
paths:
  live:
    source: publisher
    publish:
      runOnInit: ffmpeg -f v4l2 -i $CAMERA_INPUT -c:v copy -f rtsp $RTSP_STREAM_URL
      runOnInitRestart: yes
EOF

# Start RTSP server in the background
$RTSP_SERVER_BINARY $RTSP_SERVER_CONFIG &
RTSP_SERVER_PID=$!

# Start HTTP server in the background
cd $HLS_OUTPUT_DIR
python3 -m http.server $HTTP_SERVER_PORT &
HTTP_SERVER_PID=$!
cd ..

# Function to clean up background processes on exit
cleanup() {
    echo "Stopping servers..."
    kill $RTSP_SERVER_PID
    kill $HTTP_SERVER_PID
    exit
}

# Trap exit signals to run cleanup
trap cleanup SIGINT SIGTERM

# Start FFmpeg to stream to RTSP and HLS
ffmpeg -f v4l2 -i $CAMERA_INPUT \
    -c:v copy -f rtsp $RTSP_STREAM_URL \
    -c:v copy -f hls -hls_time 2 -hls_list_size 5 -hls_flags delete_segments \
    $HLS_OUTPUT_DIR/$HLS_PLAYLIST
