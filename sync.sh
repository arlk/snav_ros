#!/bin/bash

APP="snav_ros"

HNAME="thiago@msi"
HOST="home/thiago/ws"

SNAME="linaro@piper"
SNAP="/home/linaro/ros_ws/src"

if [ "$1" == "push" ]; then
  set -x
  rsync -auzP --exclude "$HOST/.git" "$HOST/$APP" "$SNAME:$SNAP"
else
  set -x
  rsync -auzP --exclude "$HNAME:$HOST/.git" "$HNAME:$HOST/$APP" $SNAP
  (cd "$SNAP/.."; catkin_make)
fi
