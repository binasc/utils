#!/bin/sh

txt=`nc -4l 0.0.0.0 10053`
ip=`echo $txt | awk '{print $1}'`
name=`echo $txt | awk '{print $2}'`

echo $ip
echo $name

`pwd`/update-ddns.sh $name $ip
