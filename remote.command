#!/bin/sh
cd $(dirname "$0")
./mythtv_irtoy -v -f config.macpro  -o $HOME/remote.log
