# 8. Pedals on the base (CAN-Bus)

Pedals plugged into the base by CAN-Bus, such as a P1000 with its light blue,
are reached through the base. Their positions are two axes of the joystick
report ([06](06-input-reports.md#pedals-on-the-base)); their settings travel in
a pass-through carried by the settings report.

Everything here is from two captures of SIMAGIC SimPro Manager and a two-pedal
P1000 (throttle and brake) on an Alpha Evo: `pedal-calibration.pcapng`,
calibrating it, and `pedal-settings.pcapng`, changing the brake's settings.

**What the app sends**, starting from what the latest capture,
`throttle-range.pcapng`, had: the settings update, for the brake and the
throttle, with any curve points, not only SimPro's four (see
[Every settings change is the same update](#every-settings-change-is-the-same-update));
and a calibration, as SimPro's went (see [`f1 20`](#f1-20-the-calibration)):
`1c`, the raw readings watched, then `20` with the new pairs, `1d`, the update,
and `16`, `1b`, `17` as captured; or, cancelled, `20` with the old pairs and
`1d`. Each saved pair is 16 counts inside what was seen at each end, and a sensor
that moved less than 200 counts keeps its old pair. See `AlphaEvoPedals.h` and
`BasePedalController.h`. It never asks for the `f6` reply and never sends
`f1 18`; it sends nothing until the user has turned pedal settings on and
changed one or started a calibration.

**Curve points** are on one line, all four presets exactly: a point is
`410 − 2.8 × output %`, 410 at 0 % and 130 at 100 %.

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
| `f1 1f` | none | right after | **brake on its load cell**; `f1 1e` is the angle sensor |
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

One group per sensor, in the order of `f1 20`'s pairs, would fit.

**Group 1 is the brake's**, from `pedal-settings.pcapng`, in which SimPro's
brake settings were changed one at a time and changed back. Nothing else in
`f1 21` moved. Its five numbers:

| Index | Linear, full range | What moves it |
| --- | --- | --- |
| 0 | 6 | the range's start: 6 at 0 %; 8, 41, 38 as the slider was dragged towards 10 % |
| 1 | 340 | the curve, at 25 % in |
| 2 | 270 | the curve, at 50 % in |
| 3 | 200 | the curve, at 75 % in |
| 4 | 302 | the range's end: 302 at 100 %; 284, then 266 dragged towards 90 % |

SimPro's four curves, as indices 1–3:

| Curve | Sent | As output, if 410 is 0 % and 130 is 100 % |
| --- | --- | --- |
| Linear | 340, 270, 200 | 25, 50, 75 % |
| Second | 382, 326, 242 | 10, 30, 60 % |
| Third | 298, 214, 158 | 40, 70, 90 % |
| Fourth | 382, 270, 158 | 10, 50, 90 % |

The output column is a reading that fits all four, not something seen: the
numbers look like positions in SimPro's own curve editor, y down, rather than
percentages. The range ends' scale, 6 to 302, is the same sort of thing; where
exactly 10 % and 90 % fall on it is not pinned, since the slider was dragged.

**Group 2 is the throttle's**, from `throttle-curves.pcapng`, in which SimPro's
throttle curve was stepped through its presets and nothing else moved. Its curve
points went through the same four values as the brake's, so the presets are the
same for both. Its range end stayed at 257, which on the brake's scale is 85 %.
`throttle-range.pcapng` confirms the throttle uses the same scale: SimPro showed
41 as about 12 %, 257 as 85 % and 299 as 99 %. Before the
capture it was on a custom curve, `340, 270, 156`.

Group 0 is linear over its whole range in every capture, as an unused clutch
would be on a set without one. Group 3, `6, 205, 270, 335, 302`, never moved.

The same capture has the brake's range start at 44, 13 %, where the two before
had 6.

Changing the display unit, % or kg, sent nothing new: it is SimPro's alone.

### Every settings change is the same update

Each change in SimPro sent, three times each: `f1 20` with the calibration as it
stands, `f1 21` with every group, then **`f1 1f`** with the brake on its load
cell or **`f1 1e`** on its angle sensor. The `f6` reply's last byte followed:
`00` for the load cell, `01` for angle. That last report may be what applies the
other two, as chunk 3 does for the wheel ([05](05-wheel-config.md#sending-rules)),
so it should never be sent alone: the wheel's block taught that sending only the
report that applies can wipe what it applies.

The calibration cannot be read back that has been found, so a host that sends
this update has to restate a calibration it keeps itself.

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
