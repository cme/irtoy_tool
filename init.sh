#!/bin/sh
cd `dirname "$0"`
while true; do
  ./mythtv_irtoy -v -f config.myth
  sleep 3
done


