#!/bin/bash
#/config/1x_prox/1x_prox >>/var/log/1x_prox.log 2>&1 &
/usr/bin/screen -S 1x_prox -d -m /config/1x_prox/1x_prox eth0 eth1


