# Flip clock firmware for ESP32-C6

An ESP-IDF v6.1 port of `../BurstClockSerialDS3231_copal227`, which stays as the reference.
The port plan is in [the ESP-IDF port plan](https://ampcode.com/artifacts/esp-idf-port-plan-cc1a787b-1f3b-4367-b82d-5e005d4a7ae6).

So far the app boots, sets its clock from the DS3231 real-time clock, drives the stepper
motor, and runs a console on the C6's built-in USB port. The commands are `help`,
`s <count>` to turn the motor, `now` to show the time, and `z <yyyymmddhhmmss>` to set the
RTC in UTC. Flipping the display to the time, and the other commands, come in later phases.

The defaults assume a Seeed Studio XIAO ESP32C6, but any ESP32-C6 board works: every pin is
a menuconfig option.

## Set up ESP-IDF

This project needs **ESP-IDF v6.1**. Check which version your shell has:

```fish
idf.py --version
```

It should print `ESP-IDF v6.1`. If it prints an older version, or `idf.py` is not found,
install v6.1 next to what you have with Espressif's installer,
[`eim`](https://docs.espressif.com/projects/idf-im-ui/en/latest/), then activate it:

```fish
eim install --idf-versions v6.1 --target esp32c6
source ~/.espressif/tools/activate_idf_v6.1.fish
```

Activate it again in each new shell. The build stops with a clear message if it finds an
older ESP-IDF.

In an Amp orb, `.agents/setup` does all this, and every shell has ESP-IDF activated.

## Build

```fish
cd esp32
idf.py build
```

The target (`esp32c6`) comes from `sdkconfig.defaults`. Everything specific to this clock is
under "Flip clock" in `idf.py menuconfig`: the motor's steps per cycle (defaults for the
Copal 227, 1280 steps per 9 minutes), the longest pause (120 minutes), under
"Stepper wiring" the coil GPIOs, the motor direction and the time between steps, and under
"Real-time clock" the I2C GPIOs, the time zone and how often system time is set from the RTC.

`sdkconfig.defaults` also turns on "Reverse the motor", because this clock's motor needed it
on the first bench test. A setting changed in menuconfig lives in `esp32/sdkconfig`, which
is not in git, so anything this clock needs belongs in `sdkconfig.defaults` too.

## Run the host tests

The parts of the firmware written in plain C, the flip rules, the stepper's step
sequencing and the DS3231's register format, have unit tests that build and run on your computer with no board attached:

```fish
cd esp32/host_test
cmake --workflow --preset host
```

This builds with AddressSanitizer and UndefinedBehaviorSanitizer and takes a couple of seconds.
It needs ESP-IDF activated only because it uses the Unity test framework that ships with ESP-IDF.

## Flash and open the console

Plug the board in by its USB port that connects to the chip itself. Boards with two USB
ports usually label it `USB`; the other one, often labelled `UART`, goes through a
USB-to-serial chip and will not show the console. Then:

```fish
cd esp32
idf.py flash monitor
```

If more than one serial device is connected, name the port, for example
`idf.py -p /dev/cu.usbmodem101 flash monitor`. `ls /dev/cu.usbmodem*` lists them on macOS,
and `ls /dev/ttyACM*` on Linux.

In the monitor, `Ctrl-T` then `Ctrl-R` restarts the board so you can see its boot messages
again, and `Ctrl-]` leaves the monitor. Type `help` at the `flipclock>` prompt.

### Known issue: line editing on ESP-IDF v6.1

ESP-IDF v6.1.0 has a bug, [espressif/esp-idf#19366](https://github.com/espressif/esp-idf/issues/19366),
that turns off line editing and history when the console is on the USB port. It shows as
`Your terminal application does not support escape sequences` at boot, and the first line
you type afterwards is refused with `Unrecognized command`. The cause: the console asks the
terminal whether it understands escape sequences before the USB driver is running, so it
never sees the answer, and the answer arrives later as if you had typed it.

The firmware works around it by asking again once the driver is running. At boot you should
see `console_cmds: Terminal answered the escape-sequence probe; line editing is on`, and no
escape-sequence warning. If you see `line editing stays off` instead, the workaround did
not help: press Enter once after each boot to clear the stray answer, and tell us.

## Bench procedure: first motor test

This checks three things nobody could check without your bench: that the motor turns the
right way, that the C6's 3.3 V outputs switch the ULN2003 board hard enough that the motor
never stalls, and that the time between steps stays close to 3 ms. It takes about half an hour.
At the end there is a list of what to send back.

### 1. Check the pins on your board

The default coil pins are GPIO 18, 19, 20 and 21. On the ESP32-C6 chip they are free for
general use: they are not strapping pins (4, 5, 8, 9, 15), USB (12, 13), the UART0 console
(16, 17) or SPI flash (24 to 30). On the Seeed Studio XIAO ESP32C6 they are the pads
D10, D8, D9 and D3; the silkscreen shows the D-numbers, not the GPIO numbers.

If your board does not bring one of them out, pick another pin that is not in that list and
not 10, 11 or 14, which some C6 chip variants do not connect. Set it in `idf.py menuconfig`
under Flip clock → Stepper wiring.

### 2. Wire it, with the USB cable unplugged

| XIAO ESP32C6 pad | ESP32-C6 pin | To the ULN2003 board | Same wire on the Nano |
| --- | --- | --- | --- |
| D10 | GPIO 18 | IN1 | D12 |
| D8 | GPIO 19 | IN2 | D11 |
| D9 | GPIO 20 | IN3 | D10 |
| D3 | GPIO 21 | IN4 | D9 |
| 5V | 5V (also labelled VBUS or VIN) | + (5–12V) | 5V |
| GND | GND | − | GND |

- The motor plugs into the ULN2003 board as before.
- The ULN2003 board and the motor run on 5 V from the board's 5V pin, which comes straight
  from the USB cable. The GPIOs are 3.3 V and only drive the IN pins.
- **Never connect 5 V to a GPIO pin.** The C6's pins are not 5 V tolerant.
- The ground wire between the two boards is required. Without it the IN signals have no
  reference and the motor will not move, or will move erratically.

### 3. Flash and check the boot messages

```fish
cd esp32
idf.py build
idf.py flash monitor
```

Press `Ctrl-T` then `Ctrl-R` if the boot messages have already scrolled past. Among them
you should see:

```text
I (...) flipclock: Stepper on GPIO 18, 19, 20, 21 (IN1-IN4), 3000 us per step, reversed
I (...) flipclock: Motor: 1280 steps per 9 minutes; pauses up to 120 minutes
flipclock>
```

If the RTC is not wired yet, there are also lines saying `ds3231` gets no answer and the
time is not trusted. That is expected until the RTC procedure below.

**Check:** all four LEDs on the ULN2003 board are off, and the motor is silent. The coils
are off from boot until the first step.

### 4. Check that bad counts are refused

Type each of these at the `flipclock>` prompt:

```text
s -5
s 0
s 1281
```

**Expected:** each one prints `Usage: s <count>, with count from 1 to 1280`, and the motor
does not move. (On the Nano, `S-5` made the motor run forever.)

### 5. Check the direction

Flip clocks only go forward: the top card on the upper half tips toward you and drops.
Type:

```text
s 50
```

**Expected:** a short whirr, then a line like `s 50: 50 steps in 147 ms, gap min 3005 us, max 3060 us`.
Watch which way the card drum turns. Repeat `s 50` until a card drops.

The firmware starts with "Reverse the motor" on, because that is what this clock needed;
the boot line ends `3000 us per step, reversed`. On another clock the other direction may
be right.

- **Cards drop after a few repeats:** the direction is right. Go on to step 6.
- **The drum turns the other way**, cards lift backwards, or nothing drops after six
  repeats: stop. Leave the monitor with `Ctrl-]`, run `idf.py menuconfig`, go to
  Flip clock → Stepper wiring, press the space bar on "Reverse the motor", then press `S`
  to save and `Q` to quit. Then run `idf.py flash monitor` again and repeat this step.
  To make that stick for a fresh checkout, also change `CONFIG_FLIPCLOCK_REVERSE` in
  `sdkconfig.defaults`.
  (Swapping the wires, IN1 with IN4 and IN2 with IN3, does the same job.)

### 6. One full cycle

Repeat `s 10` until a card has just dropped, so the mechanism starts just past a flip.
Then type:

```text
s 1280
```

**Expected:** about 4 seconds of steady motor sound, with a card dropping roughly every
half second while the motor runs: nine cards in all, the last one just before the motor
stops. The line it prints should look like:

```text
s 1280: 1280 steps in 3846 ms, gap min 3004 us, max 3090 us
```

- The step count must be exactly 1280.
- `gap min` must be 3000 or slightly above. Anything below about 2900 means the motor gets
  steps too close together, and we need to know.
- `gap max` should stay within a few hundred microseconds of 3000. An occasional larger gap
  is harmless to the motor but tell us about it.

### 7. Stalls on 3.3 V logic

Write down the time the clock shows. Then run `s 1280` five times in a row. That is 6400
steps, which should drop 45 cards.

**Expected:** the clock now shows exactly 45 minutes later than what you wrote down, and
the motor sounded the same every time.

**A stall** sounds like buzzing or grinding while the drum stays still, and it shows up as
fewer cards dropping. If the clock shows fewer than 45 minutes later, run `s 10` until the
next card drops and count how many times it took; that tells us roughly how many steps
were lost.

If you hear stalls, try a slower step: in `idf.py menuconfig` under Flip clock → Stepper
wiring, set "Time between steps" to 4000, flash again, and repeat this step. Tell us
whether that cured it.

### 8. Send back

Paste these into the thread:

1. Which C6 board you have (name or a link), and the GPIOs you used if you changed any.
2. The two boot lines from step 3, and whether the ULN2003 LEDs were all off.
3. The output of the three commands in step 4.
4. Whether the motor turned the right way first time, or you set "Reverse the motor" or swapped wires.
5. Every `s 1280` line from steps 6 and 7.
6. The clock's reading before and after step 7, and anything unusual you heard. If you
   changed the step time, say what to and what happened.

## Bench procedure: real-time clock

This checks that the board reads the DS3231 at boot and shows the right local time, that
`z` sets it, that the time survives losing power, and that the firmware notices when the
backup battery has been out. It takes about 20 minutes. You need a phone or computer
showing the correct time, and a screwdriver or fingernail to get the coin cell out.

### 1. Wire it, with the USB cable unplugged

| XIAO ESP32C6 pad | ESP32-C6 pin | To the ZS-042 (DS3231) module |
| --- | --- | --- |
| D4 (labelled SDA) | GPIO 22 | SDA |
| D5 (labelled SCL) | GPIO 23 | SCL |
| 3V3 | 3.3 V | VCC |
| GND | GND | GND |

- **The module now runs on 3.3 V, not 5 V as on the Nano.** Its SDA and SCL pull-up
  resistors go to its VCC pin, so on 5 V it would drive 5 V into the C6's pins, which are
  not 5 V tolerant. Move that wire from 5V to 3V3.
- Leave SQW and 32K unconnected. The firmware does not use them.
- The ULN2003 board stays on 5V, as in the motor procedure.
- Keep the coin cell in for now.

### 2. Boot

```fish
cd esp32
idf.py build
idf.py flash monitor
```

Press `Ctrl-T` then `Ctrl-R` to see the boot messages from the start. Look for these lines:

```text
I (...) flipclock: RTC on SDA GPIO 22, SCL GPIO 23; time zone EST5EDT,M3.2.0,M11.1.0
I (...) clock_time: System time set from the RTC: 2026-11-13 17:45:09 UTC, local 2026-11-13 12:45:09 EST
```

- If the module came out of the running Nano clock, it already holds UTC, and the local
  time should be right without setting anything.
- A line like `ds3231: control register 0x05 -> 0x04` may appear once. It turns off the
  once-a-minute alarm the Nano version switched on, and makes sure the clock keeps running on
  the battery.
- `ds3231: ... no answer at address 0x68` means the board cannot see the module: check that
  SDA and SCL are not swapped, and that VCC and GND are connected.
- `RTC oscillator stop flag is set` followed by `Time not trusted` means the module has lost
  its time at some point. That is fine: step 4 sets it.

### 3. Check the time and the time zone

Type `now`. The output looks like:

```text
UTC    2026-11-13 17:45:09
Local  2026-11-13 12:45:09 EST
RTC    2026-11-13 17:45:09 UTC, oscillator ok
Time is trusted.
```

**Check:** `Local` matches your phone to within a couple of seconds, and the zone name is
right (EST in winter, EDT in summer). The firmware assumes US Eastern time. If the clock
lives somewhere else, tell us where; the zone is a menuconfig setting.

### 4. Set the time

First check that impossible dates are refused. Each of these should print the `Usage: z ...`
message and change nothing:

```text
z 20261313120000
z 20270229120000
z 2026111317
```

Then, in a second terminal on your computer, print the current UTC time as the command needs it:

```fish
date -u +%Y%m%d%H%M%S
```

Type `z `, then that number, at the `flipclock>` prompt, and press Enter. It will be behind
by however long that took. To do better, add 15 to the seconds (carrying into the minutes
if needed), type the command, and press Enter when your phone shows that second.

**Expected:** it prints the same four lines as `now`, ending with `oscillator ok` and
`Time is trusted.`, and `Local` matches your phone.

### 5. Power cycle

Leave the monitor with `Ctrl-]`, unplug the USB cable, wait a minute, and plug it back in.
Then reopen the monitor without flashing:

```fish
idf.py monitor
```

Press `Ctrl-T` then `Ctrl-R` to restart the board while you watch.

**Expected:** the boot shows `System time set from the RTC` with the right time, and `now`
matches your phone to within a couple of seconds.

### 6. Battery out

1. Leave the monitor with `Ctrl-]` and unplug the USB cable.
2. Take the coin cell out of the module, wait ten seconds, and put it back.
3. Plug the USB cable in, run `idf.py monitor`, and press `Ctrl-T` then `Ctrl-R`.

**Expected:** the boot shows `RTC oscillator stop flag is set` and
`Time not trusted. Set it with: z <yyyymmddhhmmss>, in UTC`, and `now` ends with
`oscillator stop flag SET` and `Time is NOT trusted`. The RTC line probably shows
2000-01-01, which is what the DS3231 resets to, and `UTC` and `Local` show 1970, because
the firmware refuses to take its time from an RTC that lost it.

Then set the time with `z` as in step 4. **Expected:** `oscillator ok` and `Time is trusted.`
Restart once more with `Ctrl-T` then `Ctrl-R`: the boot should show `System time set from the RTC`.

### 7. Send back

Paste these into the thread:

1. The boot lines from step 2 that start with `flipclock`, `clock_time` or `ds3231`.
2. The `now` output from step 3, the time your phone showed, and where the clock lives if
   the zone is wrong.
3. The output of every `z` command from step 4.
4. The boot line and the `now` output from step 5.
5. The boot lines and the `now` output from step 6, before and after setting the time.
6. Optional: if you leave the board running for a few hours, it logs
   `Resynced from the RTC; system time was ... ms off` every hour. Paste a few of those;
   they show how fast the C6's own clock drifts.
