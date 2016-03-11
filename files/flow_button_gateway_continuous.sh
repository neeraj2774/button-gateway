#!/bin/sh /etc/rc.common

while true
do
	line=$(ps | grep 'flow_button_gateway_appd' | grep -v grep)
	if [ -z "$line" ]
	then
		/etc/init.d/flow_button_gateway_appd start
	else
		sleep 5
	fi
done

