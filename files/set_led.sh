#!/bin/sh

VALUE=$1
PIN=76

# check if the gpio has already been exported
ls /sys/class/gpio/gpio$PIN > /dev/null

if [ $? -ne 0 ];then
    echo $PIN > /sys/class/gpio/export
    # check for any error, some gpio cannot be exported
    if [ $? -ne 0 ];then
        exit 1
    fi
fi

# set pin direction as out
DIRECTION=`cat /sys/class/gpio/gpio$PIN/direction`
if [ "$DIRECTION" != "out" ]; then
    echo out > /sys/class/gpio/gpio$PIN/direction
fi

echo $VALUE > /sys/class/gpio/gpio$PIN/value

