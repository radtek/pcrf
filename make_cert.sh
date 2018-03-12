#!/bin/bash

openssl req -new -batch -x509 -days 3650 -nodes     \
   -newkey rsa:1024 -out /usr/local/etc/freeDiameter/cert/cert.pem -keyout /usr/local/etc/freeDiameter/cert/privkey.pem \
   -subj /CN=pcrf2.tattelecom.ru
openssl dhparam -out /usr/local/etc/freeDiameter/cert/dh.pem 1024
