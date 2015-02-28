#!/bin/sh

ip=`curl -s http://curlmyip.com`
name="home.binasc.com"
remote=binasc.com
remote_port=10053

echo $ip $name | nc -4 $remote $remote_port
