#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Raul Segui
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build gt_neo_survey.html: the GT Neo drawing with a letter on every control,
and a form to note which joystick button each one presses.

    python3 tools/gt_neo_survey/make_survey.py

Temporary helper for mapping the GT Neo's inputs; not part of the app.
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SVG = ROOT / "src/devices/simagic_alpha_evo/ui/wheels/gt_neo.svg"
OUT = Path(__file__).resolve().parent / "gt_neo_survey.html"

BUTTON = ["press"]
THUMB = ["up", "down"]
ENCODER = ["clockwise", "anticlockwise"]
SEVEN_WAY = ["up", "down", "left", "right", "push", "clockwise", "anticlockwise"]

# letter, svg id, name, actions, badge position (mm, as in the svg)
LEFT = [
    ("A", "button_arm_top_left", "Left arm, top button", BUTTON, (-107.5, -67.4)),
    ("B", "button_arm_middle_left", "Left arm, middle button", BUTTON, (-88.5, -59.2)),
    ("C", "button_arm_bottom_left", "Left arm, bottom button", BUTTON, (-77, -41.5)),
    ("D", "thumb_encoder_left", "Left thumb encoder (red roller)", THUMB, (-84, -18)),
    ("E", "button_side_left", "Left side button (◀)", BUTTON, (-76.2, 0.2)),
    ("F", "switch_7way_left", "Left 7-way switch", SEVEN_WAY, (-54.7, -2.3)),
    ("G", "button_lower_left", "Left lower button (EXIT)", BUTTON, (-71.2, 26)),
    ("H", "encoder_upper_left", "Upper-left rotary knob", ENCODER, (-26.7, -18)),
    ("I", "encoder_lower_left", "Lower-left rotary knob", ENCODER, (-23.4, 35.6)),
]
RIGHT = [
    ("J", "button_arm_top_right", "Right arm, top button (PIT)", BUTTON),
    ("K", "button_arm_middle_right", "Right arm, middle button (DRS)", BUTTON),
    ("L", "button_arm_bottom_right", "Right arm, bottom button (KERS)", BUTTON),
    ("M", "thumb_encoder_right", "Right thumb encoder (red roller)", THUMB),
    ("N", "button_side_right", "Right side button (▶)", BUTTON),
    ("O", "switch_7way_right", "Right 7-way switch", SEVEN_WAY),
    ("P", "button_lower_right", "Right lower button (⏻)", BUTTON),
    ("Q", "encoder_upper_right", "Upper-right rotary knob", ENCODER),
    ("R", "encoder_lower_right", "Lower-right rotary knob", ENCODER),
]
PADDLES = [
    ("S", "paddle_shift_left", "Left shift paddle (behind the wheel)", BUTTON, (-140, -25)),
    ("T", "paddle_shift_right", "Right shift paddle (behind the wheel)", BUTTON, (140, -25)),
    ("U", "paddle_clutch_left", "Left clutch paddle (blank if it only moves an axis)", BUTTON, (-140, 36)),
    ("V", "paddle_clutch_right", "Right clutch paddle (blank if it only moves an axis)", BUTTON, (140, 36)),
]

controls = list(LEFT)
for (letter, key, name, actions), (_, _, _, _, (x, y)) in zip(RIGHT, LEFT):
    controls.append((letter, key, name, actions, (-x, y)))
controls += PADDLES

svg = SVG.read_text(encoding="utf-8")
svg = re.sub(r"<\?xml[^>]*\?>", "", svg)
svg = re.sub(r"<!--.*?-->", "", svg, flags=re.S)
svg = svg.replace('width="320mm" height="180mm"', 'width="100%"')

badges = []
for letter, _, _, _, (x, y) in controls:
    badges.append(
        f'<g class="badge" data-letter="{letter}"><circle cx="{x}" cy="{y}" r="5.2"/>'
        f'<text x="{x}" y="{y}">{letter}</text></g>'
    )
svg = svg.replace("</svg>", '<g id="survey-badges">' + "".join(badges) + "</g></svg>")

rows = []
for letter, key, name, actions, _ in controls:
    fields = "".join(
        f'<label>{"" if a == "press" else a}<input data-key="{key}" data-action="{a}" '
        f'inputmode="numeric" size="3"></label>'
        for a in actions
    )
    rows.append(f'<tr data-letter="{letter}"><th>{letter}</th><td>{name}</td><td>{fields}</td></tr>')

html = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>GT Neo button survey</title>
<style>
 body {{ font-family: sans-serif; margin: 1em auto; max-width: 1100px; background: #1b1c20; color: #e6e6ea; }}
 svg {{ display: block; background: #111; border-radius: 8px; }}
 .badge circle {{ fill: #fff; stroke: #000; stroke-width: 0.6; opacity: 0.92; }}
 .badge text {{ fill: #000; font-size: 6px; font-weight: bold; text-anchor: middle; dominant-baseline: central; }}
 .badge.hl circle {{ fill: #ffd400; }}
 table {{ border-collapse: collapse; width: 100%; margin-top: 1em; }}
 th, td {{ border-bottom: 1px solid #333; padding: 6px; text-align: left; }}
 tr.hl {{ background: #3a3410; }}
 label {{ margin-right: 12px; white-space: nowrap; }}
 input {{ margin-left: 4px; width: 3em; }}
 textarea {{ width: 100%; height: 12em; font-family: monospace; }}
 button {{ font-size: 1.1em; padding: 6px 14px; }}
</style></head><body>
<h1>GT Neo button survey</h1>
<p>In HID Center, open <b>SIMAGIC Alpha Evo → Wheel</b>. Press each control one at a time and type the
number that lights up into its box. Knobs and rollers: turn one click each way. 7-way switches: push in
each direction, press down, and turn. Leave a box empty if nothing lights up. Clicking a row highlights
its letter on the drawing.</p>
{svg}
<table>{''.join(rows)}</table>
<p><label>Anything else (controls missing from the drawing, ones that light two numbers, …)<br>
<textarea id="notes" style="height:4em"></textarea></label></p>
<p><button id="make">Make text to send</button></p>
<textarea id="out" readonly placeholder="Press the button above, then copy all of this and send it."></textarea>
<script>
const KEY = "gt-neo-survey";
const inputs = [...document.querySelectorAll("input[data-key]")];
const notes = document.getElementById("notes");
const saved = JSON.parse(localStorage.getItem(KEY) || "{{}}");
for (const i of inputs) i.value = saved[i.dataset.key + "/" + i.dataset.action] || "";
notes.value = saved.notes || "";
function store() {{
  const s = {{ notes: notes.value }};
  for (const i of inputs) s[i.dataset.key + "/" + i.dataset.action] = i.value.trim();
  localStorage.setItem(KEY, JSON.stringify(s));
}}
document.addEventListener("input", store);
function highlight(letter) {{
  document.querySelectorAll(".hl").forEach(e => e.classList.remove("hl"));
  document.querySelectorAll(`[data-letter="${{letter}}"]`).forEach(e => e.classList.add("hl"));
}}
for (const r of document.querySelectorAll("tr[data-letter]")) {{
  r.addEventListener("click", () => highlight(r.dataset.letter));
  r.addEventListener("focusin", () => highlight(r.dataset.letter));
}}
document.getElementById("make").onclick = () => {{
  const lines = [];
  const by = {{}};
  for (const i of inputs) (by[i.dataset.key] ||= []).push(i);
  for (const [key, list] of Object.entries(by)) {{
    const parts = list.map(i => (i.dataset.action === "press" ? "" : i.dataset.action + ":") + (i.value.trim() || "-"));
    lines.push(key + " " + parts.join(" "));
  }}
  if (notes.value.trim()) lines.push("notes: " + notes.value.trim());
  const out = document.getElementById("out");
  out.value = lines.join("\\n");
  out.select();
}};
</script>
</body></html>
"""
OUT.write_text(html, encoding="utf-8")
print(f"wrote {OUT}")
