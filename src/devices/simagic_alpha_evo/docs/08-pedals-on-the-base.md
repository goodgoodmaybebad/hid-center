# 8. Pedals on the base (CAN-Bus)

Pedals plugged into the base by CAN-Bus, such as a P1000 with its light blue,
are reached through the base. Their positions are two axes of the joystick
report ([06](06-input-reports.md#pedals-on-the-base)); their settings travel in
a pass-through carried by the settings report.

Everything here is from one capture, `pedal-calibration.pcapng`: SIMAGIC SimPro
Manager calibrating a two-pedal P1000 (throttle and brake) on an Alpha Evo. **No
part of it is implemented**, and none of it has been sent by this app.

What SimPro had the brake set to throughout, by its owner: **load cell** mode
(the other is angle, the second sensor), shown in **%** (the other is kg), a
**linear** curve (one of four), and the whole range, **0 % to 100 %**. Nothing
was changed but the calibration, so no byte below is tied to any of these yet.

## The pass-through

`SET_FEATURE(report 0x80)`, 64 bytes, sub-command **`0x1d`**, zero-padded:

| Offset | Size | Value | Notes |
| --- | --- | --- | --- |
| 0 | 1 | `0x80` | report id |
| 1 | 1 | `0x1d` | sub-command: pass-through |
| 2 | 1 | `0x0a` | always; perhaps the pedals' CAN address |
| 3 | 1 | `0x01` | always on the way out (`0x02` in replies) |
| 4 | 1 | N | length of what follows, from offset 5 |
| 5 | 1 | `0xf1` or `0xf6` | `0xf6` asks for a reply; `0xf1` does not get one |
| 6 | N − 1 | | command byte, then its data |

Replies are read with `GET_FEATURE(report 0x82)`, 64 bytes, laid out the same
way: `82 1d 0a 02 N f6 …`.

**Every `0xf1` command is sent three times**, about 100 ms apart. Whether once
is enough is not known.

## Commands seen

| Command | Data | When | Taken to be |
| --- | --- | --- | --- |
| `f6 00` | none | once a second, all the time | read the pedals' state; reply below |
| `f1 18` | `00` (N = 3) | about 26 times a second, all the time | not known; a keep-alive? |
| `f1 1c` | none | when calibration started | **start calibration** |
| `f1 20` | 16 bytes | when calibration was saved | **the calibration**: see below |
| `f1 1d` | none | right after | **end calibration** |
| `f1 20` | the same 16 bytes | again, after `1d` | |
| `f1 21` | 40 bytes | right after | settings; not decoded |
| `f1 1f` | none | right after | not known |
| `f1 16` | 12 zero bytes | right after | not known |
| `f1 1b` | 12 bytes | right after | settings; the same bytes as the `f6` reply's |
| `f1 17` | 5 zero bytes | right after | not known |

After a calibration SimPro restates every setting, `20` to `17`, whether or not
each is needed. Which of them make the calibration stick is not known.

### `f6` reply

```
82 1d 0a 02 1c f6 01 00 00 00 00 00 00
04 ff 68 00 64 04 04 ff 00 00 64 04      ← the same 12 bytes as `f1 1b`'s data
00 00 00 00 00 00 00 00
9c 00 01 01                              ← also in `f1 21`'s third group
```

It did not change across the calibration.

### `f1 20`: the calibration

Four pairs of 16-bit little-endian numbers, each a sensor's lowest and highest
raw reading:

| Pair | Captured | Sensor |
| --- | --- | --- |
| 0 | 1544 – 2407 | none seen moving: perhaps a clutch, which this set has not got |
| 1 | 1461 – 2011 | the brake's first sensor (raw bytes 30–31, below) |
| 2 | 922 – 2160 | the brake's second sensor (raw bytes 33–34) |
| 3 | 1523 – 2490 | the throttle (raw bytes 32–33) |

The brake has two sensors, both moving when it is pressed: a P1000's brake has a
Hall angle sensor and a load cell. Which pair is which of the two is not known.

Each pair is a little inside what the pedals were seen to reach during the run
(1445 – 2011, 906 – 2182, 1512 – 2506): SimPro keeps a margin of up to about 20
counts. Whether that is a rule or its own sampling is not known.

### `f1 21`

Ten 16-bit little-endian numbers in four groups of five:

```
(6, 340, 270, 200, 302)
(6, 340, 270, 200, 302)
(6, 340, 270, 156, 257)
(6, 205, 270, 335, 302)
```

One group per sensor, in the order of `f1 20`'s pairs, would fit. What the
numbers mean is not known: a capture of SimPro changing one pedal setting at a
time would tell.

### `f1 1b`

```
04 ff 68 00 64 04 | 04 ff 00 00 64 04
```

Two groups of six would fit one per pedal. `0x64` is 100 in both, which would
fit a range ending at 100 %, and the second group's `00 00` one starting at 0 %.
Neither is confirmed.

## Raw readings during calibration

Between `f1 1c` and `f1 1d`, bytes 30–34 of joystick report `0x01`, as received,
stop carrying the calibrated pedal axes and carry the sensors' raw readings
instead, 12 bits each:

| Sensor | Bits | Range seen |
| --- | --- | --- |
| Brake, first | byte 30, and the low nibble of byte 31 | 1445 – 2011 |
| Throttle | byte 32, and the low nibble of byte 33 | 1512 – 2506 |
| Brake, second | the high nibble of byte 33, and byte 34 | 906 – 2182 |

The high nibble of byte 31 reads 8 throughout. Outside calibration the same bytes
are the calibrated axes, 0 to 4095. These offsets are from the report as
received, not from its descriptor, which the capture does not hold.
