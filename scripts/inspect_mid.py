#!/usr/bin/env python3
"""Inspect the MIDIs in BlueSlaveP4/data/mid/ and show exactly the 16x16 grid
that the P4 firmware will display after parsing + GM-drum mapping + bar-fold.

This mirrors the logic in BlueSlaveP4/src/mem_midi_loader.cpp so you can verify
files *before* running Upload Filesystem P4.

Usage:
    python scripts/inspect_mid.py BlueSlaveP4/data/mid
    python scripts/inspect_mid.py path/to/single.mid
    python scripts/inspect_mid.py                      # defaults to BlueSlaveP4/data/mid

Requires no third-party library; uses its own minimal SMF parser.
"""

from __future__ import annotations
import os
import sys
import struct
from pathlib import Path
from typing import List, Tuple, Optional

# Force UTF-8 on Windows terminals so star glyphs render correctly.
if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    try:
        sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
    except Exception:
        pass

# ---------------------------------------------------------------------------
# GM drum note -> track index table (MUST stay in sync with mem_midi_loader.cpp)
# 0xFF => ignore
# ---------------------------------------------------------------------------
NOTE_MAP = [
    0, 0, 6, 1, 4, 1,  11, 2, 11, 2, 11, 3,   # 35..46
    13, 10, 9, 10, 9, 9, 9, 8, 9, 5, 9, 7,    # 47..58
    9, 14, 15, 14, 14, 15, 10, 11, 7, 7, 8, 8, 7, 7, 8, 8, 7, 7, 7, 12, 12, 7, 7,  # 59..81
]

TRACK_NAMES = ["BD","SD","CH","OH","CP","CB","RS","CL",
               "MA","CY","HT","LT","MC","MT","HC","LC"]


def gm_note_to_track(note: int) -> int:
    if 35 <= note <= 81:
        return NOTE_MAP[note - 35]
    return 0xFF


# Per-channel mapping mirrors mem_midi_loader.cpp: each MIDI channel
# (except ch9 which uses GM drums) is assigned one distinct kit slot.
CH_TO_TRACK = [1, 11, 2, 3, 4, 5, 6, 7, 8, 0, 9, 10, 13, 12, 14, 15]

def channel_to_track(ch: int) -> int:
    return CH_TO_TRACK[ch & 0x0F]


def pitched_note_to_track(note: int) -> int:
    """Legacy pitch-based mapping (kept for reference, no longer used)."""
    if note < 36: return 0
    if note < 42: return 15
    if note < 48: return 11
    if note < 54: return 13
    if note < 60: return 10
    if note < 63: return 1
    if note < 66: return 6
    if note < 69: return 4
    if note < 72: return 8
    if note < 75: return 2
    if note < 78: return 14
    if note < 81: return 12
    if note < 84: return 3
    if note < 87: return 5
    if note < 90: return 7
    return 9


class SMFParser:
    def __init__(self, data: bytes):
        self.d = data
        self.p = 0

    def u8(self) -> int:
        v = self.d[self.p]; self.p += 1; return v

    def u16(self) -> int:
        v = struct.unpack(">H", self.d[self.p:self.p+2])[0]; self.p += 2; return v

    def u32(self) -> int:
        v = struct.unpack(">I", self.d[self.p:self.p+4])[0]; self.p += 4; return v

    def vlq(self) -> int:
        v = 0
        for _ in range(4):
            b = self.u8()
            v = (v << 7) | (b & 0x7F)
            if not (b & 0x80):
                return v
        return v

    def skip(self, n: int):
        self.p += n


def parse_file(path: Path, midi_channel: Optional[int]
               ) -> Tuple[int, List[Tuple[int,int]], int]:
    """Return (tpq, events[(tick,trk)], tempo_us). tempo_us=0 if not found."""
    data = path.read_bytes()
    if len(data) < 14 or data[:4] != b"MThd":
        return (0, [], 0)
    sm = SMFParser(data)
    sm.p = 4
    hdr_len = sm.u32()
    fmt = sm.u16(); (_fmt := fmt)
    ntracks = sm.u16()
    tpq = sm.u16()
    if tpq & 0x8000:
        return (0, [], 0)
    if tpq == 0:
        tpq = 96
    if hdr_len > 6:
        sm.skip(hdr_len - 6)

    events: List[Tuple[int,int]] = []
    tempo_us = 0

    for _t in range(ntracks):
        if sm.p + 8 > len(data):
            break
        if data[sm.p:sm.p+4] != b"MTrk":
            break
        sm.p += 4
        tlen = sm.u32()
        end = sm.p + tlen
        tick = 0
        status = 0
        while sm.p < end:
            tick += sm.vlq()
            b = sm.u8()
            if b == 0xFF:
                meta_type = sm.u8()
                ml = sm.vlq()
                if meta_type == 0x51 and ml == 3 and tempo_us == 0:
                    t = (sm.u8() << 16) | (sm.u8() << 8) | sm.u8()
                    if t > 0:
                        tempo_us = t
                else:
                    sm.skip(ml)
                continue
            if b == 0xF0 or b == 0xF7:
                sm.skip(sm.vlq()); continue
            if 0xF8 <= b <= 0xFE:
                continue
            if b & 0x80:
                status = b; d1 = sm.u8()
            else:
                d1 = b
            ev = status & 0xF0
            ch = status & 0x0F
            if ev == 0x90 or ev == 0x80:
                vel = sm.u8()
                is_on = (ev == 0x90 and vel >= 16)
                trk = 0xFF
                if is_on:
                    if ch == 9:
                        if midi_channel in (9, None) or midi_channel == -1:
                            trk = gm_note_to_track(d1)
                    else:
                        if midi_channel is None or midi_channel == ch or midi_channel == -1:
                            trk = channel_to_track(ch)
                if trk != 0xFF:
                    events.append((tick, trk))
            elif ev in (0xA0, 0xB0, 0xE0):
                sm.u8()
            # 0xC0,0xD0: d1 already consumed
        sm.p = end

    return (tpq, events, tempo_us)


def fold_events_to_16(tpq: int, events: List[Tuple[int,int]]
                      ) -> Tuple[List[List[bool]], int]:
    grid = [[False]*16 for _ in range(16)]
    if not events or tpq <= 0:
        return grid, 16
    bar_ticks = tpq * 4
    min_tick = min(e[0] for e in events)
    max_tick = max(e[0] for e in events)
    bar_start = (min_tick // bar_ticks) * bar_ticks
    span_ticks = max_tick - bar_start
    num_bars = (span_ticks // bar_ticks) + 1
    num_bars = max(1, min(4, num_bars))
    raw_len = num_bars * 16
    for tick, trk in events:
        if tick < bar_start:
            continue
        rel = tick - bar_start
        step = (rel * 16) // bar_ticks
        if step < raw_len and 0 <= trk < 16:
            grid[trk][step % 16] = True
    return grid, raw_len


def inspect(path: Path) -> bool:
    tpq, events, tempo_us = parse_file(path, midi_channel=None)
    used_fallback = False
    if tpq == 0:
        print(f"  [ERR] not a valid SMF: {path.name}")
        return False
    if not events:
        tpq, events, tempo_us = parse_file(path, midi_channel=None)
        used_fallback = True
    grid, raw_len = fold_events_to_16(tpq, events)
    bpm = (60_000_000 / tempo_us) if tempo_us else 0.0
    total = sum(1 for row in grid for c in row if c)
    tracks_used = sum(1 for row in grid if any(row))

    print(f"\n=== {path.name} ===")
    print(f"  tpq={tpq} events={len(events)} tempo_us={tempo_us} "
          f"bpm={bpm:.2f} raw_len={raw_len} tracks_used={tracks_used} "
          f"hits={total}" + ("  [fallback: all channels]" if used_fallback else ""))
    header = "    step:  " + " ".join(f"{i+1:2d}" for i in range(16))
    print(header)
    for t in range(16):
        row = " ".join(" *" if grid[t][s] else " ." for s in range(16))
        marker = "+" if any(grid[t]) else " "
        print(f"   {marker}{TRACK_NAMES[t]:>3} {t:2d} |{row}")
    return True


def main():
    default = Path(__file__).resolve().parent.parent / "BlueSlaveP4" / "data" / "mid"
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else default
    if not target.exists():
        print(f"Path not found: {target}")
        sys.exit(1)
    if target.is_file():
        ok = inspect(target)
        sys.exit(0 if ok else 1)
    files = sorted(p for p in target.iterdir()
                   if p.is_file() and p.suffix.lower() == ".mid")
    if not files:
        print(f"No .mid files in {target}")
        sys.exit(0)
    print(f"Scanning {len(files)} MIDI file(s) in {target}")
    failed = 0
    for p in files:
        if not inspect(p):
            failed += 1
    print(f"\nDone. {len(files)-failed} OK, {failed} failed.")

if __name__ == "__main__":
    main()
