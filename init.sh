#!/bin/sh
cd `dirname "$0"`
while true; do
  ./irtoy_tool -v -f config.macpro
  sleep 3
done


