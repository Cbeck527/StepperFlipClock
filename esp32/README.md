# Flip clock firmware for ESP32-C6

An ESP-IDF v6.1 port of `../BurstClockSerialDS3231_copal227`, which stays as the reference.
The port plan is in [the ESP-IDF port plan](https://ampcode.com/artifacts/esp-idf-port-plan-cc1a787b-1f3b-4367-b82d-5e005d4a7ae6).

So far the app boots, drives the stepper motor, and runs a console on the C6's built-in USB
port with two commands: `help`, and `s <count>` to turn the motor. The clock itself, the RTC
and the other commands come in later phases.

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
Copal 227, 1280 steps per 9 minutes), the longest pause (120 minutes), and under
"Stepper wiring" the coil GPIOs, the motor direction and the time between steps.

## Run the host tests

The parts of the firmware written in plain C, the flip rules and the stepper's step
sequencing, have unit tests that build and run on your computer with no board attached:

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

## Bench procedure: first motor test

This checks three things nobody could check without your bench: that the motor turns the
right way, that the C6's 3.3 V outputs switch the ULN2003 board hard enough that the motor
never stalls, and that the time between steps stays close to 3 ms. It takes about half an hour.
At the end there is a list of what to send back.

### 1. Check the pins on your board

The default coil pins are GPIO 18, 19, 20 and 21. On the ESP32-C6 chip they are free for
general use: they are not strapping pins (4, 5, 8, 9, 15), USB (12, 13), the UART0 console
(16, 17) or SPI flash (24 to 30). Find them on your board's pinout or silkscreen.

If your board does not bring one of them out, pick another pin that is not in that list and
not 10, 11 or 14, which some C6 chip variants do not connect. Set it in `idf.py menuconfig`
under Flip clock → Stepper wiring.

### 2. Wire it, with the USB cable unplugged

| From the ESP32-C6 board | To the ULN2003 board | Same wire on the Nano |
| --- | --- | --- |
| GPIO 18 | IN1 | D12 |
| GPIO 19 | IN2 | D11 |
| GPIO 20 | IN3 | D10 |
| GPIO 21 | IN4 | D9 |
| 5V (also labelled VBUS or VIN) | + (5–12V) | 5V |
| GND | − | GND |

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
I (...) flipclock: Stepper on GPIO 18, 19, 20, 21 (IN1-IN4), 3000 us per step
I (...) flipclock: Motor: 1280 steps per 9 minutes; pauses up to 120 minutes
flipclock>
```

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

- **Cards drop after a few repeats:** the direction is right. Go on to step 6.
- **The drum turns the other way**, cards lift backwards, or nothing drops after six
  repeats: stop. Leave the monitor with `Ctrl-]`, run `idf.py menuconfig`, go to
  Flip clock → Stepper wiring, press the space bar on "Reverse the motor", then press `S`
  to save and `Q` to quit. Then run `idf.py flash monitor` again and repeat this step.
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
