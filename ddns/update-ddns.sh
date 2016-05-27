#!/bin/sh

domain=$1
ip=$2
file="/etc/dnsmasq.d/ddns.conf"
if ! [ -z "$3" ]; then
    file=$3;
fi

old_ip=`sed -n "s/^address=\/$domain\/\(.*\)$/\1/p" $file`
if [ "$old_ip" != "$ip" ]; then
    sed -i "s/\(\/$domain\/\).*$/\1$ip/" $file
    service dnsmasq restart
fi
