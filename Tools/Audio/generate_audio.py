"""Generate Fairhaven's sound effects and ambience procedurally.

Runs under plain CPython with numpy, like the terrain build. Writing the audio
here rather than sourcing clips keeps the project free of third-party assets and
makes every sound a few tunable numbers.

    python Tools/Audio/generate_audio.py [--out DIR]

Ambient beds are crossfaded end-to-start so they loop without a seam.
"""
from __future__ import annotations

import argparse
import math
import os
import wave

import numpy as np

RATE = 44100


def log(message):
    print("[audio] " + message, flush=True)


# ---------------------------------------------------------------------------
# Primitives
# ---------------------------------------------------------------------------
def noise(length, seed):
    rng = np.random.default_rng(seed)
    return rng.uniform(-1.0, 1.0, size=length).astype(np.float32)


def lowpass_fast(signal, cutoff_hz):
    """Vectorised approximation of repeated one-pole smoothing."""
    window = max(1, int(RATE / max(cutoff_hz, 1.0)))
    kernel = np.ones(window, dtype=np.float32) / window
    return np.convolve(signal, kernel, mode="same").astype(np.float32)


def highpass_fast(signal, cutoff_hz):
    return (signal - lowpass_fast(signal, cutoff_hz)).astype(np.float32)


def envelope(length, attack, decay, power=2.0):
    """Simple attack/decay envelope over ``length`` samples."""
    attack_n = max(1, int(attack * RATE))
    decay_n = max(1, length - attack_n)
    rise = np.linspace(0.0, 1.0, attack_n, dtype=np.float32)
    fall = np.linspace(1.0, 0.0, decay_n, dtype=np.float32) ** power
    return np.concatenate([rise, fall])[:length]


def seamless(signal, fade_seconds=1.2):
    """Crossfade the tail into the head so the clip loops without a click."""
    fade = int(fade_seconds * RATE)
    if fade * 2 >= signal.shape[0]:
        return signal
    head = signal[:fade].copy()
    tail = signal[-fade:].copy()
    ramp = np.linspace(0.0, 1.0, fade, dtype=np.float32)
    blended = tail * (1.0 - ramp) + head * ramp
    out = signal[:-fade].copy()
    out[:fade] = blended
    return out


def normalise(signal, peak=0.85):
    maximum = float(np.max(np.abs(signal))) or 1.0
    return (signal / maximum * peak).astype(np.float32)


def write_wav(path, signal):
    data = np.clip(normalise(signal), -1.0, 1.0)
    pcm = (data * 32767.0).astype("<i2")
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(pcm.tobytes())
    log("wrote %s (%.1fs)" % (os.path.basename(path), len(signal) / float(RATE)))


# ---------------------------------------------------------------------------
# Sounds
# ---------------------------------------------------------------------------
def footstep(seed, bright=1400.0, body=260.0, length=0.16):
    n = int(length * RATE)
    raw = noise(n, seed)
    crack = highpass_fast(raw, bright) * envelope(n, 0.004, length, power=6.0)
    thud = lowpass_fast(raw, body) * envelope(n, 0.006, length, power=3.0)
    return crack * 0.55 + thud * 0.9


def footstep_water(seed):
    n = int(0.28 * RATE)
    raw = noise(n, seed)
    splash = highpass_fast(raw, 900.0) * envelope(n, 0.006, 0.28, power=3.0)
    gulp = lowpass_fast(raw, 420.0) * envelope(n, 0.02, 0.28, power=2.0)
    return splash * 0.8 + gulp * 0.5


def jump_effort(seed):
    n = int(0.22 * RATE)
    t = np.arange(n, dtype=np.float32) / RATE
    tone = np.sin(2.0 * math.pi * (180.0 - 60.0 * t / 0.22) * t).astype(np.float32)
    breath = highpass_fast(noise(n, seed), 700.0)
    return (tone * 0.25 + breath * 0.5) * envelope(n, 0.01, 0.22, power=3.0)


def land_thud(seed):
    n = int(0.3 * RATE)
    raw = noise(n, seed)
    return lowpass_fast(raw, 200.0) * envelope(n, 0.004, 0.3, power=4.0)


def wind_bed(seed, seconds=14.0):
    n = int(seconds * RATE)
    base = lowpass_fast(noise(n, seed), 700.0)
    body = lowpass_fast(noise(n, seed + 1), 180.0)
    # Slow gusting.
    t = np.arange(n, dtype=np.float32) / RATE
    gust = (0.55 + 0.45 * np.sin(2.0 * math.pi * 0.045 * t + 1.1)
            * np.sin(2.0 * math.pi * 0.017 * t))
    return seamless((base * 0.5 + body * 0.9) * gust)


def ocean_bed(seed, seconds=16.0):
    n = int(seconds * RATE)
    surf = lowpass_fast(noise(n, seed), 900.0)
    deep = lowpass_fast(noise(n, seed + 3), 150.0)
    t = np.arange(n, dtype=np.float32) / RATE
    # Sets of waves rolling in every few seconds.
    swell = 0.35 + 0.65 * (0.5 + 0.5 * np.sin(2.0 * math.pi * 0.13 * t))** 2
    swell = swell * (0.7 + 0.3 * np.sin(2.0 * math.pi * 0.041 * t + 0.7))
    return seamless(surf * swell * 0.8 + deep * 0.7)


def birdsong(seed, seconds=18.0):
    n = int(seconds * RATE)
    out = np.zeros(n, dtype=np.float32)
    rng = np.random.default_rng(seed)
    for _ in range(34):
        start = int(rng.uniform(0.0, seconds - 1.0) * RATE)
        chirps = int(rng.integers(2, 5))
        base = float(rng.uniform(2100.0, 4200.0))
        for c in range(chirps):
            offset = start + int(c * rng.uniform(0.06, 0.13) * RATE)
            length = int(rng.uniform(0.035, 0.075) * RATE)
            if offset + length >= n:
                break
            t = np.arange(length, dtype=np.float32) / RATE
            sweep = base * (1.0 + rng.uniform(-0.28, 0.34) * t / max(t[-1], 1e-6))
            tone = np.sin(2.0 * math.pi * sweep * t).astype(np.float32)
            out[offset:offset + length] += tone * envelope(length, 0.006, 0.05, 2.0) * 0.5
    return seamless(out, fade_seconds=1.5)


def town_murmur(seed, seconds=15.0):
    n = int(seconds * RATE)
    bed = lowpass_fast(noise(n, seed), 340.0) * 0.5
    rng = np.random.default_rng(seed + 5)
    for _ in range(26):
        start = int(rng.uniform(0.0, seconds - 1.0) * RATE)
        length = int(rng.uniform(0.25, 0.8) * RATE)
        if start + length >= n:
            continue
        t = np.arange(length, dtype=np.float32) / RATE
        freq = float(rng.uniform(110.0, 260.0))
        voice = np.sin(2.0 * math.pi * freq * t) * 0.12
        voice += np.sin(2.0 * math.pi * freq * 2.03 * t) * 0.05
        bed[start:start + length] += voice * envelope(length, 0.08, 0.6, 1.5)
    return seamless(bed)


def stream_bed(seed, seconds=12.0):
    n = int(seconds * RATE)
    trickle = highpass_fast(lowpass_fast(noise(n, seed), 2600.0), 500.0)
    body = lowpass_fast(noise(n, seed + 2), 700.0)
    return seamless(trickle * 0.7 + body * 0.5)


def ui_click(seed, freq=1500.0, length=0.05):
    n = int(length * RATE)
    t = np.arange(n, dtype=np.float32) / RATE
    tone = np.sin(2.0 * math.pi * freq * t).astype(np.float32)
    return tone * envelope(n, 0.002, length, power=5.0)


def ui_confirm(seed):
    n = int(0.2 * RATE)
    t = np.arange(n, dtype=np.float32) / RATE
    a = np.sin(2.0 * math.pi * 880.0 * t) * envelope(n, 0.004, 0.2, 4.0)
    b = np.sin(2.0 * math.pi * 1320.0 * t) * envelope(n, 0.05, 0.2, 4.0)
    return (a * 0.6 + b * 0.5).astype(np.float32)


def interact_click(seed):
    n = int(0.12 * RATE)
    raw = noise(n, seed)
    return (highpass_fast(raw, 1200.0) * envelope(n, 0.002, 0.12, 5.0) * 0.8
            + lowpass_fast(raw, 300.0) * envelope(n, 0.003, 0.12, 4.0) * 0.5)


SOUNDS = [
    ("S_FootstepGround", lambda: footstep(11)),
    ("S_FootstepWater", lambda: footstep_water(13)),
    ("S_Jump", lambda: jump_effort(17)),
    ("S_Land", lambda: land_thud(19)),
    ("S_Interact", lambda: interact_click(23)),
    ("S_UIClick", lambda: ui_click(29)),
    ("S_UIConfirm", lambda: ui_confirm(31)),
    ("A_Wind", lambda: wind_bed(37)),
    ("A_Ocean", lambda: ocean_bed(41)),
    ("A_Birds", lambda: birdsong(43)),
    ("A_Town", lambda: town_murmur(47)),
    ("A_Stream", lambda: stream_bed(53)),
]


def main():
    parser = argparse.ArgumentParser(description="Generate Fairhaven audio.")
    parser.add_argument("--out", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "Output"))
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    for name, builder in SOUNDS:
        write_wav(os.path.join(args.out, name + ".wav"), builder())
    log("%d sounds written to %s" % (len(SOUNDS), args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
