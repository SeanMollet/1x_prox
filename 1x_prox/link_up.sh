#!/bin/vbash
/sbin/dhclient -r -cf /var/run/dhclient_eth0_0.conf -pf /var/run/dhclient_eth0_0.pid -lf /var/run/dhclient_eth0_0.leases eth0.0
/sbin/dhclient -q -nw -cf /var/run/dhclient_eth0_0.conf -pf /var/run/dhclient_eth0_0.pid -lf /var/run/dhclient_eth0_0.leases eth0.0
