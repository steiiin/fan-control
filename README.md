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
Usage: fan-control <command>
Commands:
  set  <0..100>   Set BOTH fans
  set1 <0..100>   Set CPU fan
  set2 <0..100>   Set GPU fan
  dump            Show CPU/GPU temps and fan status
  power           Automatic: 40% minimum, 100% target at 80°C
  default         Automatic: original curve, 100% target at 85°C
  silent          Automatic: 20% through 50°C, 100% target at 90°C
  auto            Select power/default/silent from highest CPU/GPU load
```

Automatic modes synchronize both fans using a rolling average of the last
three hotter CPU/GPU readings, sampled once per second. Invalid GPU readings
are ignored; an invalid CPU reading immediately requests 100% duty in every mode.

Choose a mode with `sudo fan-control power`, `sudo fan-control default`, or
`sudo fan-control silent`. These commands take no additional arguments.
Use `sudo fan-control auto` to select the temperature curve from system load.
Use `sudo fan-control default` to retain the fixed default curve.

### Load-based AUTO selection

AUTO reads CPU and GPU utilization approximately once per second and uses
the higher reading, `max(CPU%, GPU%)`, without further smoothing:

| Highest load | Selected curve |
|---|---|
| Above 90% | power |
| Below 40% | silent |
| 40% through 90%, inclusive | default |

For example, CPU at 100% and GPU at 0% gives a load of 100%, selecting power.
Comparisons use unrounded readings. There is no hysteresis or minimum time
between profile changes. If only one reading is available, AUTO uses that
reading; if neither is available, it selects default. A valid 0% reading
counts as available.

CPU utilization is calculated from successive `/proc/stat` counters across
all cores, treating idle and I/O wait as inactive. Its first reading establishes
a baseline and is unavailable for selection. GPU utilization comes from
`nvidia-smi` with a 500 ms query timeout, falling back to DRM device
`gpu_busy_percent` files where supported. The highest valid GPU percentage
from the selected source is used. Missing or invalid readings appear as `N/A`.
No additional library is required; explicit power/default/silent modes do not
query utilization.

AUTO starts both fans at 40%. Each selected profile still uses the temperature
curves, rolling temperature average, and duty ramp below. Switching profiles
preserves temperature history and current duty; entering power enforces its
40% minimum immediately. Invalid CPU temperature still requests 100% duty.
The status line shows CPU/GPU load, their maximum (`MAX`), and the active profile.

### Temperature curves

Power starts at 40% duty and holds that minimum through 40°C. Silent starts
at 20% and holds that minimum through 50°C. Default retains its original
40% startup duty and piecewise-linear curve through (°C, %):
`(45, 20), (55, 30), (60, 40), (70, 50), (80, 70), (85, 100)`.

The new curves use smoothstep, `s(x) = x²(3 − 2x)`:

- Power: normalize `x = (temperature − 40) / 40`, then use `40 + 60 × s(x)`.
- Silent: normalize `x = (temperature − 50) / 40`, then use `20 + 80 × s(x²)`.

Clamp `x` to `[0, 1]` before evaluation. Round targets to whole percentages,
capping them at 99% below each mode's full-speed temperature. The curves
join smoothly to their constant endpoints, with power always at least as
high as default and silent always at most as high as default.

| Averaged temperature | Power | Default | Silent |
|---|---:|---:|---:|
| 40°C | 40% | 20% | 20% |
| 50°C | 49% | 25% | 20% |
| 60°C | 70% | 40% | 21% |
| 70°C | 91% | 50% | 33% |
| 80°C | 100% | 70% | 67% |
| 85°C | 100% | 100% | 89% |
| 90°C | 100% | 100% | 100% |

These are **target duties**. Actual duty rises by up to 1 percentage point
per iteration below 70°C, 2 below 80°C, and 4 from 80°C onward, and falls by
1 point per iteration. The full-speed thresholds do not bypass this ramp
or temperature averaging. Power never falls below 40%; the other modes
never fall below 20% during automatic operation.

### Tests

The tests cover the curves, default compatibility, mode selection, CLI
validation, utilization parsing, subprocess failures/timeouts, and controller
state across profile changes without accessing fan hardware or requiring root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
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

The example selects the temperature curve dynamically from CPU/GPU load.
To select a fixed curve, use
`ExecStart=/usr/local/bin/fan-control power` or
`ExecStart=/usr/local/bin/fan-control silent`. An explicit
`ExecStart=/usr/local/bin/fan-control default` retains the original default curve.
