#!/bin/bash
if (( $# != 1 )); then
    echo "Syntax: ./zen-bclk-oc <bclk-khz>"
    exit
fi
echo "options zen-bclk-oc bclk-khz=$1" > /etc/modprobe.d/zen-bclk-oc.conf