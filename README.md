# About this repository
This repository contains:

- protocol definitions
- signaling servers
- client side libraries

to establish p2p connections.  
## Peer Linker
peer-linker is a data relay server that two peers can use to exchange SDP and other data.  
A peer can establish relay connection by registering itself as a pad in the peer-linker and linking it to another pad.
## Channel Hub
channel-hub is an auxiliary server that helps peers to dynamically create pads.  
A peer registers a channel in the channel-hub. Other peers can send pad creation requests to the peer hosting the channel via the channel-hub.

# Self hosting guide of peer-linker and channel-hub
## Install
### dependencies
- libwebsockets
### build
```
# clone this repository
git clone --recursive https://github.com/mojyack/peer-linker
cd peer-linker
# build
meson setup build -Dbuildtype=release -Dclient=false
ninja -C build
```

## Create SSL certificate
If your domain does not have an ssl certificate, you can use a self-signed certificate.
```
openssl req -new -newkey rsa:4096 -days 36500 -nodes -x509 -keyout ssl.key -out ssl.cert
```

## Create user certificate
If you want to restrict users, you can issue user certificates.
### Choose format
You can choose any format you like for the content of the certificate.  

For example: cert-content.txt
```
user=example
```
### Write verifier
Once you have decided on the format of the certificate, you should also create a program to verify it.  
It can be a native binary, a shell script or a Python program.  
The content of the certificate is passed to the verification program as the first argument.  
The program must exit with code 0 if the certificate is valid, or with any other code if it is not valid.

For example: verify.sh
```
#!/bin/bash
if [[ $1 == "user=example" ]]; then
    echo "welcome, example!"
    exit 0
else
    echo "unknown user"
    exit 1
fi
```
### Create certificate
First, create the server's private key:
```
dd if=/dev/random of=private-key.bin bs=16 count=1
```
Then, create user certificate:
```
build/session-key-util private-key.bin cert-content.txt > user-cert.txt
```
Give the generated user certificate(user-cert.txt) to the user.

## Start server
Now you can start the server like this command:
```
build/peer-linker \
    --key private-key.bin \
    --cert-verifier verify.sh \
    --ssl-cert ssl.cert \
    --ssk-key ssl.key
```
