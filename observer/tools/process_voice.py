#!/usr/bin/env python3
"""THE OBSERVER - voice post-processing.

The Witness speaks with the protagonist's voice, so its lines start from the
same TTS takes and are made WRONG here: layered detune, timing smear, a cold
room around the words. The dictaphone 'wrong' take is the same principle at
a subtler dose.

Run from observer/:  python3 tools/process_voice.py
"""
import os
import wave

import numpy as np

DIR = "assets/audio/voice"


def load(path):
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        n = w.getnframes()
        ch = w.getnchannels()
        raw = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64) / 32768.0
        if ch == 2:
            raw = raw.reshape(-1, 2).mean(axis=1)
    return raw, sr


def save(path, x, sr):
    peak = np.max(np.abs(x)) or 1.0
    pcm = (x / peak * 0.85 * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def resample(x, ratio):
    """Cheap linear resample: ratio > 1 = higher pitch / shorter."""
    n = int(len(x) / ratio)
    idx = np.linspace(0, len(x) - 1, n)
    return np.interp(idx, np.arange(len(x)), x)


def lowpass(x, sr, cutoff):
    alpha = 1.0 - np.exp(-2 * np.pi * cutoff / sr)
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += alpha * (x[i] - acc)
        y[i] = acc
    return y


def highpass(x, sr, cutoff):
    return x - lowpass(x, sr, cutoff)


def echo(x, sr, delay, fb, mixv):
    d = int(delay * sr)
    y = np.copy(x)
    buf = np.zeros(len(x) + d * 6)
    buf[: len(x)] = x
    for i in range(d, len(buf)):
        buf[i] += buf[i - d] * fb
    return y * (1 - mixv) + buf[: len(x)] * mixv


def mixpad(*layers):
    n = max(len(x) for x in layers)
    out = np.zeros(n)
    for x in layers:
        out[: len(x)] += x
    return out


def wobble(x, sr, depth=0.012, rate=1.7):
    """Slow tape-style pitch flutter via time-varying resample."""
    t = np.arange(len(x)) / sr
    mod = 1.0 + depth * np.sin(2 * np.pi * rate * t) + depth * 0.4 * np.sin(2 * np.pi * rate * 3.1 * t)
    pos = np.cumsum(mod)
    pos = pos / pos[-1] * (len(x) - 1)
    return np.interp(pos, np.arange(len(x)), x)


def main():
    # ---- dictaphone: honest take, then the take that is not yours
    x, sr = load(f"{DIR}/dicta_raw.wav")
    hiss = np.random.default_rng(7).standard_normal(len(x)) * 0.012
    dicta = lowpass(highpass(x, sr, 220), sr, 3400) + hiss
    save(f"{DIR}/dicta.wav", dicta, sr)

    wrong = resample(x, 0.955)                        # slightly deeper, longer
    wrong = wobble(wrong, sr, 0.02, 1.1)
    tail = resample(x, 0.5)[::-1] * 0.10              # faint reversed under-voice
    wrong = mixpad(wrong, np.pad(tail, (int(0.15 * sr), 0)))
    wrong = lowpass(highpass(wrong, sr, 180), sr, 2900)
    wrong = echo(wrong, sr, 0.11, 0.25, 0.18)
    hiss2 = np.random.default_rng(8).standard_normal(len(wrong)) * 0.02
    save(f"{DIR}/dicta_wrong.wav", wrong + hiss2, sr)

    # ---- the Witness's line: your voice, from a colder room
    x, sr = load(f"{DIR}/witness_final_raw.wav")
    a = resample(x, 1.0)
    b = np.pad(resample(x, 2 ** (-3 / 12.0)), (int(0.020 * sr), 0)) * 0.55
    c = np.pad(resample(x, 2 ** (0.3 / 12.0)), (int(0.008 * sr), 0)) * 0.30
    d = np.pad(resample(x, 0.5), (int(0.05 * sr), 0)) * 0.18
    v = mixpad(a, b, c, d)
    v = lowpass(v, sr, 3800)
    v = echo(v, sr, 0.23, 0.42, 0.30)
    v = wobble(v, sr, 0.006, 0.7)
    save(f"{DIR}/witness_final.wav", v, sr)

    # ---- whispers: strip the voice down to breath
    for src, dst in [("whisper_look_raw", "whisper_look"), ("whisper_showed_raw", "whisper_showed")]:
        x, sr = load(f"{DIR}/{src}.wav")
        w = highpass(x, sr, 1400)
        n = np.random.default_rng(9).standard_normal(len(x))
        w = w * 0.7 + np.abs(x) * n * 0.5               # breath-noise keyed to the words
        w = lowpass(w, sr, 7000)
        w = echo(w, sr, 0.16, 0.35, 0.25)
        save(f"{DIR}/{dst}.wav", w, sr)

    # cleanup raws so the shipped tree stays tidy
    for f in ["dicta_raw", "witness_final_raw", "whisper_look_raw", "whisper_showed_raw"]:
        p = f"{DIR}/{f}.wav"
        if os.path.exists(p):
            os.remove(p)
    print("voice processing complete")


if __name__ == "__main__":
    main()
