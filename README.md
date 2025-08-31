# Fan Control on Linux

This is a fork of "Clevo Fan Control on Linux";
removed the GUI bits and make it work with my GPU Fan.

```
This is tested and running on my Gigabyte G5 MF5 - with a RTX 4050.
I don't know, if it work anywhere else - or damage anything else.
```

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```
You'll need "sudo" to access the EC.

---

You can install it only for your user, but "sudo" is later
nethertheless necessary - and needs the absolute path:
```
cmake --install build --prefix "$HOME/.local"
```
fan-control will be installed in "~/.local/bin/fan-control".


## Using it

```
Usage: fan-cli <command>
Commands:
  set  <0..100>   Set BOTH fans
  set1 <0..100>   Set CPU fan
  set2 <0..100>   Set GPU fan
  dump            Show CPU/GPU temps and fan status
  auto            Auto mode (independent control)
```

## Service

To start the auto-mode on startup create a new ".service"-file:


```
/etc/systemd/system/fan-control.service
```
```
[Unit]
Description=Fan Control Gigabyte
After=multi-user.target

[Service]
Type=simple
ExecStart=/usr/local/bin/fan-control auto
Restart=on-failure
User=root

[Install]
WantedBy=multi-user.target
```