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
cmake --install build --prefix "$HOME/.local"
```
