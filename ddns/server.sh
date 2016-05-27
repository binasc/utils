#!/bin/sh

while [ 1 ]
do
    txt=`nc -4l 0.0.0.0 10053`
    ip=`echo $txt | awk '{print $1}'`
    name=`echo $txt | awk '{print $2}'`

    echo `date` accept from $ip, try to update $name

    `pwd`/update-ddns.sh $name $ip
done
