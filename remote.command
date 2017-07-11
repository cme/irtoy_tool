#!/bin/sh
cd $(dirname "$0")
./irtoy_tool -v -f config.macpro  -o $HOME/remote.log
