#!/usr/bin/env python3
"""THE OBSERVER - procedural audio synthesis.

Every sound effect and ambient bed in the game is synthesized here from
first principles (a custom-engine game deserves a custom foley bench).
Voice lines are generated separately via TTS and post-processed by
tools/process_voice.py.

Run from observer/:  python3 tools/gen_audio.py
"""
import os
import wave

import numpy as np

SR = 44100
OUT_SFX = "assets/audio/sfx"
OUT_AMB = "assets/audio/ambient"

rng = np.random.default_rng(1961)


def save(path, data, stereo=False):
    data = np.asarray(data, dtype=np.float64)
    peak = np.max(np.abs(data)) or 1.0
    data = data / peak * 0.86
    pcm = (data * 32767).astype(np.int16)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(2 if stereo else 1)
        w.setsampwidth(2)
        w.setframerate(SR)
        if stereo and pcm.ndim == 1:
            pcm = np.stack([pcm, pcm], -1)
        w.writeframes(pcm.tobytes())


def t(dur):
    return np.linspace(0, dur, int(SR * dur), endpoint=False)


def env(n, a=0.005, d=0.1, sustain=0.0, r=0.05, total=None):
    """Simple ADSR-ish envelope over n samples."""
    total = total or n
    e = np.zeros(n)
    ai, di, ri = int(a * SR), int(d * SR), int(r * SR)
    ai = max(1, min(ai, n))
    e[:ai] = np.linspace(0, 1, ai)
    rest = n - ai
    if rest > 0:
        di = min(di, rest)
        e[ai:ai + di] = np.linspace(1, sustain, di)
        e[ai + di:] = sustain
    if ri > 0 and ri < n:
        e[-ri:] *= np.linspace(1, 0, ri)
    return e


def lowpass(x, alpha):
    y = np.empty_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc += alpha * (x[i] - acc)
        y[i] = acc
    return y


def lowpass_f(x, cutoff):
    alpha = 1.0 - np.exp(-2 * np.pi * cutoff / SR)
    return lowpass(x, alpha)


def highpass_f(x, cutoff):
    return x - lowpass_f(x, cutoff)


def bandpass(x, lo, hi):
    return highpass_f(lowpass_f(x, hi), lo)


def noise(n):
    return rng.standard_normal(n)


def knock(freq=95.0, dur=0.22, tone=1.0):
    tt = t(dur)
    body = np.sin(2 * np.pi * freq * tt * (1 - 0.25 * tt)) * np.exp(-tt * 26)
    click = highpass_f(noise(len(tt)), 900) * np.exp(-tt * 90) * 0.5
    return body * tone + click


def thud(freq=60, dur=0.35, amt=1.0):
    tt = t(dur)
    return (np.sin(2 * np.pi * freq * tt * (1 - 0.4 * tt)) * np.exp(-tt * 14) * amt
            + lowpass_f(noise(len(tt)), 300) * np.exp(-tt * 20) * 0.7)


def creak(dur=1.1, f0=280, wobble=7.0):
    tt = t(dur)
    f = f0 * (1 + 0.25 * np.sin(2 * np.pi * wobble * tt) * np.exp(-tt))
    ph = np.cumsum(f) / SR
    saw = 2 * (ph % 1.0) - 1
    grit = bandpass(noise(len(tt)), 700, 2600) * 0.35
    e = env(len(tt), a=0.05, d=dur * 0.7, sustain=0.25, r=0.3)
    return (saw * 0.5 + grit) * e


def silence(dur):
    return np.zeros(int(SR * dur))


def seq(*parts):
    return np.concatenate(parts)


def mix(*layers):
    n = max(len(x) for x in layers)
    out = np.zeros(n)
    for x in layers:
        out[: len(x)] += x
    return out


def loopify(x, fade=1.5):
    """Crossfade tail into head so the buffer loops cleanly."""
    nf = int(fade * SR)
    if nf * 2 > len(x):
        nf = len(x) // 4
    ramp = np.linspace(0, 1, nf)
    x[:nf] = x[:nf] * ramp + x[-nf:] * (1 - ramp)
    return x[:-nf]


def drips(dur, rate=0.25, bright=2400):
    n = int(SR * dur)
    out = np.zeros(n)
    count = int(dur * rate)
    for _ in range(count):
        pos = rng.integers(0, n - SR // 2)
        f = rng.uniform(bright * 0.7, bright * 1.3)
        d = rng.uniform(0.08, 0.2)
        tt = t(d)
        ping = np.sin(2 * np.pi * f * tt) * np.exp(-tt * 40) * rng.uniform(0.1, 0.3)
        out[pos:pos + len(ping)] += ping
    return out


# ================================================================== SFX =====
def build_sfx():
    # footsteps
    for name, cutoff, f, amp in [
        ("step_carpet", 350, 55, 0.7), ("step_wood", 1200, 95, 1.0),
        ("step_concrete", 2400, 75, 1.0), ("step_lino", 1800, 85, 0.9),
    ]:
        tt = t(0.16)
        hit = lowpass_f(noise(len(tt)), cutoff) * np.exp(-tt * 42)
        tone = np.sin(2 * np.pi * f * tt) * np.exp(-tt * 40) * 0.5
        save(f"{OUT_SFX}/{name}.wav", (hit + tone) * amp)

    save(f"{OUT_SFX}/door_open.wav", seq(creak(0.7, 240), knock(70, 0.15, 0.4) * 0.4))
    save(f"{OUT_SFX}/door_close.wav", seq(creak(0.35, 200) * 0.5, thud(65, 0.3)))
    save(f"{OUT_SFX}/door_creak.wav", creak(1.6, 260, 5.0))
    save(f"{OUT_SFX}/door_slam.wav", mix(thud(52, 0.5, 1.4), highpass_f(noise(int(SR * 0.2)), 1500) * np.exp(-t(0.2) * 30) * 0.6))
    save(f"{OUT_SFX}/door_locked.wav", seq(knock(210, 0.09, 0.7), silence(0.06), knock(190, 0.09, 0.7), silence(0.05), knock(205, 0.12, 0.7)))
    save(f"{OUT_SFX}/unlock.wav", seq(knock(500, 0.06, 0.5), silence(0.09), knock(320, 0.1, 0.8)))
    save(f"{OUT_SFX}/pickup.wav", mix(bandpass(noise(int(SR * 0.12)), 800, 3000) * np.exp(-t(0.12) * 30), np.sin(2 * np.pi * 1200 * t(0.08)) * np.exp(-t(0.08) * 60) * 0.3))
    save(f"{OUT_SFX}/paper.wav", bandpass(noise(int(SR * 0.5)), 1400, 6000) * env(int(SR * 0.5), 0.02, 0.3, 0.25, 0.15) * (0.6 + 0.4 * np.sin(2 * np.pi * 13 * t(0.5))))
    save(f"{OUT_SFX}/switch_click.wav", seq(knock(900, 0.03, 0.35), silence(0.02), knock(600, 0.05, 0.5)))
    save(f"{OUT_SFX}/clipboard.wav", seq(knock(700, 0.04, 0.4), bandpass(noise(int(SR * 0.25)), 1400, 5000) * np.exp(-t(0.25) * 16) * 0.5))
    save(f"{OUT_SFX}/camera_up.wav", bandpass(noise(int(SR * 0.22)), 500, 2400) * env(int(SR * 0.22), 0.02, 0.15, 0.2, 0.05))
    save(f"{OUT_SFX}/camera_down.wav", bandpass(noise(int(SR * 0.18)), 400, 1800) * env(int(SR * 0.18), 0.01, 0.12, 0.15, 0.05))
    save(f"{OUT_SFX}/camera_shutter.wav", seq(knock(1400, 0.03, 0.3), knock(400, 0.05, 0.8), silence(0.05), bandpass(noise(int(SR * 0.12)), 900, 3200) * np.exp(-t(0.12) * 24) * 0.5))
    whir_t = t(0.9)
    whir = np.sin(2 * np.pi * (110 + 12 * np.sin(2 * np.pi * 6 * whir_t)) * whir_t) * 0.4 + bandpass(noise(len(whir_t)), 300, 1200) * 0.25
    save(f"{OUT_SFX}/polaroid.wav", whir * env(len(whir_t), 0.05, 0.6, 0.5, 0.2))
    crt_t = t(0.7)
    save(f"{OUT_SFX}/crt_on.wav", mix(np.sin(2 * np.pi * 15625 * crt_t) * 0.08 * env(len(crt_t), 0.3, 0.2, 0.8, 0.1), thud(120, 0.2, 0.6), noise(len(crt_t)) * np.exp(-crt_t * 18) * 0.2))
    save(f"{OUT_SFX}/crt_off.wav", mix(np.sin(2 * np.pi * 15625 * t(0.3)) * 0.06 * np.exp(-t(0.3) * 12), thud(90, 0.25, 0.5)))
    save(f"{OUT_SFX}/static_burst.wav", noise(int(SR * 0.5)) * env(int(SR * 0.5), 0.004, 0.3, 0.3, 0.15))
    save(f"{OUT_SFX}/knock_triple.wav", seq(knock(), silence(0.16), knock(90), silence(0.17), knock(85, 0.3)))
    ping_t = t(0.8)
    save(f"{OUT_SFX}/pipe_knock.wav", (np.sin(2 * np.pi * 340 * ping_t) + 0.5 * np.sin(2 * np.pi * 890 * ping_t)) * np.exp(-ping_t * 9))
    ring_t = t(1.0)
    bell = np.sin(2 * np.pi * 480 * ring_t) + np.sin(2 * np.pi * 620 * ring_t)
    trem = 0.5 + 0.5 * np.sign(np.sin(2 * np.pi * 20 * ring_t))
    save(f"{OUT_SFX}/phone_ring.wav", seq(bell * trem * 0.5 * env(len(ring_t), 0.01, 0.8, 0.8, 0.15), silence(1.6)))
    busy_t = t(0.35)
    save(f"{OUT_SFX}/phone_dead.wav", seq(*([np.sin(2 * np.pi * 425 * busy_t) * env(len(busy_t), 0.01, 0.3, 0.9, 0.03), silence(0.3)] * 3)))
    save(f"{OUT_SFX}/breaker_thunk.wav", mix(thud(70, 0.4, 1.2), knock(350, 0.08, 0.9)))
    pd_t = t(1.4)
    hum_f = 100 * np.clip(1 - pd_t * 1.1, 0, 1) ** 2
    save(f"{OUT_SFX}/power_down.wav", mix(np.sin(2 * np.pi * np.cumsum(hum_f) / SR) * np.exp(-pd_t * 2) * 0.7, thud(48, 0.5, 0.8)))
    ev_t = t(4.0)
    save(f"{OUT_SFX}/elevator_move.wav", (lowpass_f(noise(len(ev_t)), 90) * 1.2 + np.sin(2 * np.pi * 47 * ev_t) * 0.3) * env(len(ev_t), 0.5, 1.0, 0.85, 1.0))
    ding_t = t(1.4)
    save(f"{OUT_SFX}/elevator_ding.wav", (np.sin(2 * np.pi * 830 * ding_t) + 0.6 * np.sin(2 * np.pi * 1245 * ding_t) + 0.3 * np.sin(2 * np.pi * 2075 * ding_t)) * np.exp(-ding_t * 4))
    steps = []
    for i in range(6):
        tt = t(0.15)
        steps.append(lowpass_f(noise(len(tt)), 900) * np.exp(-tt * 40) * (0.8 - i * 0.06))
        steps.append(silence(0.24))
    save(f"{OUT_SFX}/stairs_transition.wav", seq(*steps, creak(0.5, 220) * 0.5, thud(65, 0.3, 0.7)))
    cry_t = t(2.8)
    cry_f = 380 + 60 * np.sin(2 * np.pi * 0.9 * cry_t) + 25 * np.sin(2 * np.pi * 5.5 * cry_t)
    cry = np.sin(2 * np.pi * np.cumsum(cry_f) / SR)
    breath = bandpass(noise(len(cry_t)), 300, 1400) * 0.3
    cry_env = (0.5 + 0.5 * np.sin(2 * np.pi * 0.55 * cry_t - 1.2)) ** 2
    save(f"{OUT_SFX}/crying.wav", lowpass_f((cry * 0.5 + breath) * cry_env, 900) * 0.8)
    fa = []
    for i in range(5):
        fa.append(lowpass_f(noise(int(SR * 0.2)), 220) * np.exp(-t(0.2) * 30))
        fa.append(silence(rng.uniform(0.3, 0.6)))
    save(f"{OUT_SFX}/footsteps_above.wav", seq(*fa))
    wt_t = t(1.6)
    riser = bandpass(noise(len(wt_t)), 200, 4000) * np.linspace(0, 1, len(wt_t)) ** 2
    save(f"{OUT_SFX}/witness_touch.wav", seq(riser[::-1] * 0.6, mix(thud(38, 0.8, 1.5), np.sin(2 * np.pi * 19 * t(0.8)) * np.exp(-t(0.8) * 4))))
    st_t = t(2.6)
    f1 = 220 * (1 + 0.5 * st_t / 2.6)
    f2 = f1 * 1.02
    f3 = f1 * 0.51
    sting = (2 * ((np.cumsum(f1) / SR) % 1) - 1) + (2 * ((np.cumsum(f2) / SR) % 1) - 1) + (2 * ((np.cumsum(f3) / SR) % 1) - 1)
    save(f"{OUT_SFX}/stinger.wav", lowpass_f(sting, 1800) * np.linspace(0.1, 1, len(st_t)) ** 1.5 * env(len(st_t), 0.1, 1.8, 0.7, 0.5))
    tvs_t = t(1.2)
    save(f"{OUT_SFX}/tv_static.wav", (noise(len(tvs_t)) * 0.7 + np.sin(2 * np.pi * 15625 * tvs_t) * 0.05) * env(len(tvs_t), 0.02, 0.9, 0.8, 0.2))


# =============================================================== ambience ===
def hum(dur, f=50.0, amt=1.0):
    tt = t(dur)
    return (np.sin(2 * np.pi * f * tt) + 0.4 * np.sin(2 * np.pi * f * 2 * tt + 0.7)
            + 0.15 * np.sin(2 * np.pi * f * 3 * tt + 1.3)) * amt


def build_ambience():
    D = 28.0
    n = int(SR * D)
    tt = t(D)

    lobby = mix(
        hum(D, 50, 0.16),
        np.sin(2 * np.pi * 100 * tt) * (0.05 + 0.03 * np.sin(2 * np.pi * 0.13 * tt)),
        lowpass_f(noise(n), 60) * 0.5,
        bandpass(noise(n), 4000, 9000) * 0.012 * (1 + np.sin(2 * np.pi * 0.07 * tt)),
    )
    save(f"{OUT_AMB}/amb_lobby.wav", loopify(lobby), stereo=True)

    hall = mix(
        lowpass_f(noise(n), 45) * 0.8,
        hum(D, 50, 0.05),
        drips(D, 0.12, 1800) * 0.4,
    )
    save(f"{OUT_AMB}/amb_hall.wav", loopify(hall), stereo=True)

    late_f = 55 * (1 + 0.01 * np.sin(2 * np.pi * 0.05 * tt))
    late = mix(
        np.sin(2 * np.pi * np.cumsum(late_f) / SR) * 0.12,
        np.sin(2 * np.pi * np.cumsum(late_f * 1.007) / SR) * 0.12,
        lowpass_f(noise(n), 38) * 0.7,
    )
    save(f"{OUT_AMB}/amb_late.wav", loopify(late), stereo=True)

    basement = mix(
        np.sin(2 * np.pi * 41 * tt) * 0.18,
        lowpass_f(noise(n), 30) * 0.9,
        drips(D, 0.4, 2600) * 0.7,
        np.sin(2 * np.pi * 120 * tt) * 0.04 * (1 + np.sin(2 * np.pi * 0.21 * tt)),
    )
    save(f"{OUT_AMB}/amb_basement.wav", loopify(basement), stereo=True)

    # the vault: beating detuned sines, slow false-choir formants
    f0 = 33.0
    anomaly = mix(
        np.sin(2 * np.pi * f0 * tt) * 0.16,
        np.sin(2 * np.pi * (f0 * 1.021) * tt) * 0.16,
        np.sin(2 * np.pi * (f0 * 2.98) * tt) * 0.05,
    )
    for formant, rate, amp in [(420, 0.031, 0.05), (860, 0.023, 0.035), (1290, 0.017, 0.02)]:
        drift = formant * (1 + 0.08 * np.sin(2 * np.pi * rate * tt))
        band = bandpass(noise(n), formant * 0.9, formant * 1.1)
        anomaly = anomaly + band * amp * (0.5 + 0.5 * np.sin(2 * np.pi * rate * 1.7 * tt))
        _ = drift
    save(f"{OUT_AMB}/amb_anomaly.wav", loopify(anomaly), stereo=True)

    birdsong = np.zeros(n)
    for _ in range(26):
        pos = rng.integers(0, n - SR)
        chirp_d = rng.uniform(0.08, 0.3)
        ct = t(chirp_d)
        cf = rng.uniform(2200, 4200) * (1 + 0.2 * np.sin(2 * np.pi * rng.uniform(8, 20) * ct))
        chirp = np.sin(2 * np.pi * np.cumsum(cf) / SR) * env(len(ct), 0.01, chirp_d * 0.6, 0.3, 0.05)
        birdsong[pos:pos + len(chirp)] += chirp * rng.uniform(0.04, 0.1)
    morning = mix(
        lowpass_f(noise(n), 120) * 0.35,
        bandpass(noise(n), 800, 2400) * 0.02,
        birdsong,
    )
    save(f"{OUT_AMB}/amb_morning.wav", loopify(morning), stereo=True)

    # wordless whisper (unintelligible sibilance with speech cadence)
    wd = 2.2
    wt = t(wd)
    syll = (0.5 + 0.5 * np.sign(np.sin(2 * np.pi * 3.3 * wt + np.sin(2 * np.pi * 1.1 * wt) * 2)))
    whisp = bandpass(noise(int(SR * wd)), 1500, 7500) * syll * env(int(SR * wd), 0.15, 1.6, 0.4, 0.4)
    save("assets/audio/voice/whisper.wav", whisp * 0.7)


if __name__ == "__main__":
    build_sfx()
    build_ambience()
    print("audio bench complete")
