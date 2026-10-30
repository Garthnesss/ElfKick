#!/usr/bin/env python3
"""ElfKick — measurement gate for the synth oscillator renders.

Reads the WAVs written by scripts/synth_harness.cpp and asserts, with
numbers (SYNTH_DESIGN.md Step 1/2 gate, oscillator level):

  * fundamental == 440 Hz +/- 1% for saw/pulse50/pulse25/tri/sine
  * saw harmonic amplitudes H2..H6 ~= 1/n of H1 within +/-30%
  * pulse 50%: H2 nulled (< -25 dB rel H1); pulse 25%: H2 present
    (> -12 dB) and H4 nulled (< -20 dB) — the pulse-width signature
  * gated_off render is exactly digital silence; saw render's tail
    (after gate-off) is exactly silent too
  * saw steady-state RMS within [-8, -3] dBFS

Exit code 0 iff all assertions pass. stdlib + numpy only.
"""
import sys
import wave

import numpy as np

SR = 48000
results = []


def check(name, ok, detail):
    results.append((name, ok, detail))
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}: {detail}")


def load(path):
    with wave.open(path, "rb") as w:
        assert w.getframerate() == SR and w.getnchannels() == 1
        raw = w.readframes(w.getnframes())
    return np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0


def steady(x, t0=0.1, t1=0.9):
    return x[int(t0 * SR):int(t1 * SR)]


def spectrum(x):
    n = 1 << 15
    seg = x[:n] * np.hanning(n)
    spec = np.abs(np.fft.rfft(seg))
    freqs = np.fft.rfftfreq(n, 1.0 / SR)
    return freqs, spec


def peak_freq(freqs, spec, lo=100.0, hi=1000.0):
    band = (freqs >= lo) & (freqs <= hi)
    idx = np.argmax(spec[band])
    f = freqs[band][idx]
    # parabolic interpolation on log magnitude
    k = np.where(band)[0][idx]
    if 0 < k < len(spec) - 1:
        a, b, c = np.log(spec[k - 1] + 1e-12), np.log(spec[k] + 1e-12), np.log(spec[k + 1] + 1e-12)
        d = 0.5 * (a - c) / (a - 2 * b + c)
        f = freqs[k] + d * (freqs[1] - freqs[0])
    return f


def harmonic_db(freqs, spec, f0, n):
    target = f0 * n
    k = np.argmin(np.abs(freqs - target))
    h1 = np.argmin(np.abs(freqs - f0))
    return 20.0 * np.log10((spec[k] + 1e-12) / (spec[h1] + 1e-12)), spec[k] / spec[h1]


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "synth-renders"
    print(f"analyzing renders in {d}")

    files = {name: load(f"{d}/{name}.wav") for name in
             ["saw_a440", "pulse50_a440", "pulse25_a440", "tri_a440", "sine_a440", "gated_off"]}

    # 1. fundamental frequency, all waveforms
    for name in ["saw_a440", "pulse50_a440", "pulse25_a440", "tri_a440", "sine_a440"]:
        freqs, spec = spectrum(steady(files[name]))
        f0 = peak_freq(freqs, spec)
        check(f"{name} fundamental", abs(f0 - 440.0) / 440.0 <= 0.01, f"measured {f0:.2f} Hz (target 440 +/-1%)")

    # 2. saw harmonic series ~= 1/n
    freqs, spec = spectrum(steady(files["saw_a440"]))
    for n in range(2, 7):
        db, ratio = harmonic_db(freqs, spec, 440.0, n)
        expected = 1.0 / n
        check(f"saw H{n} ~= 1/{n}", abs(ratio - expected) / expected <= 0.30,
              f"measured ratio {ratio:.3f} vs expected {expected:.3f} ({db:.1f} dB)")

    # 3. pulse-width signatures
    freqs, spec = spectrum(steady(files["pulse50_a440"]))
    db2_50, _ = harmonic_db(freqs, spec, 440.0, 2)
    check("pulse50 H2 nulled (50% duty)", db2_50 < -25.0, f"H2/H1 = {db2_50:.1f} dB (gate < -25 dB)")

    freqs, spec = spectrum(steady(files["pulse25_a440"]))
    db2_25, _ = harmonic_db(freqs, spec, 440.0, 2)
    db4_25, _ = harmonic_db(freqs, spec, 440.0, 4)
    check("pulse25 H2 present (25% duty)", db2_25 > -12.0, f"H2/H1 = {db2_25:.1f} dB (gate > -12 dB)")
    check("pulse25 H4 nulled (25% duty)", db4_25 < -20.0, f"H4/H1 = {db4_25:.1f} dB (gate < -20 dB)")

    # 4. silence: gated-off render + saw tail after gate-off
    g = files["gated_off"]
    check("gated_off is exact silence", float(np.max(np.abs(g))) == 0.0,
          f"max|sample| = {float(np.max(np.abs(g))):.6f} over {len(g)} samples")
    tail = files["saw_a440"][int(1.05 * SR):int(1.25 * SR)]
    check("saw tail silent after gate-off", float(np.max(np.abs(tail))) == 0.0,
          f"max|sample| = {float(np.max(np.abs(tail))):.6f} in 1.05-1.25 s window")

    # 5. saw steady-state level
    rms = float(np.sqrt(np.mean(steady(files["saw_a440"]) ** 2)))
    rms_db = 20.0 * np.log10(rms + 1e-12)
    check("saw steady RMS in [-8,-3] dBFS", -8.0 <= rms_db <= -3.0, f"RMS = {rms_db:.2f} dBFS")

    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} assertions passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
