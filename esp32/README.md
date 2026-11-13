# Flip clock firmware for ESP32-C6

An ESP-IDF v6.1 port of `../BurstClockSerialDS3231_copal227`, which stays as the reference.
The port plan is in [the ESP-IDF port plan](https://ampcode.com/artifacts/esp-idf-port-plan-cc1a787b-1f3b-4367-b82d-5e005d4a7ae6).

So far this is the skeleton: the app boots, logs the motor configuration, and runs a console
on the C6's built-in USB port. It has only `help`; the clock commands come in later phases.

## Set up ESP-IDF

Install ESP-IDF v6.1 with Espressif's installer, [`eim`](https://docs.espressif.com/projects/idf-im-ui/en/latest/):

```fish
eim install --idf-versions v6.1 --target esp32c6
```

Then activate it in each new shell:

```fish
source ~/.espressif/tools/activate_idf_v6.1.fish
```

In an Amp orb, `.agents/setup` does both, and every shell has ESP-IDF activated.

## Build

```fish
cd esp32
idf.py build
```

The target (`esp32c6`) comes from `sdkconfig.defaults`. The motor settings and the
longest pause (120 minutes) are under "Flip clock" in `idf.py menuconfig`; the motor defaults
are for the Copal 227 (1280 steps per 9 minutes).

## Run the host tests

`components/flip_logic` is plain C with no ESP-IDF headers, so its tests build and run on
your computer with no board attached:

```fish
cd esp32/components/flip_logic/host_test
cmake --workflow --preset host
```

This builds with AddressSanitizer and UndefinedBehaviorSanitizer and takes a couple of seconds.
It needs ESP-IDF activated only because it uses the Unity test framework that ships with ESP-IDF.

## Flash and open the console

Plug the board in by its USB port (the one wired to the chip, not a USB-to-UART bridge), then:

```fish
cd esp32
idf.py flash monitor
```

If more than one serial device is connected, name the port, for example
`idf.py -p /dev/cu.usbmodem101 flash monitor`; `ls /dev/cu.usbmodem*` lists them on macOS.
Type `help` at the `flipclock>` prompt. Press `Ctrl-]` to leave the monitor.
