#!/usr/bin/env python3
"""ElfKick — measurement gate for the Step 2 synth VOICE renders.

Reads the WAVs written by the SynthHarness app (Kick/apps/synth_harness)
and asserts the SYNTH_DESIGN.md Step 2 gate at voice level:

  * osc-1-only A440: fundamental 440 Hz +/-1% (FFT peak, parabolic
    interpolation) and via zero-crossing rate (+/-2%); saw harmonics
    H2..H6 ~= 1/n within +/-30% (filter wide open in this render)
  * sustain: steady-state RMS (0.6-1.1 s, gate held) > -20 dBFS
  * release: RMS 0.5 s after the release stage ends (gate off at 1.2 s,
    release 0.30 s -> check 2.0 s region end / 1.95 s tail) < -60 dBFS
  * triad 60/64/67: FFT energy at 261.63 / 329.63 / 392.00 Hz each
    within 6 dB of the strongest peak in the 200-500 Hz band
  * filter: spectral centroid of the cutoff=200 Hz render is less than
    half the centroid of the cutoff=12000 Hz render
  * full patch A440 peak < 1.2 (headroom sanity, design section 10)

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


def rms_db(x):
    return 20.0 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)


def spectrum(x, t0, t1):
    seg = x[int(t0 * SR):int(t1 * SR)]
    n = 1 << 15
    seg = seg[:n] * np.hanning(min(len(seg), n))
    spec = np.abs(np.fft.rfft(seg))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / SR)
    return freqs, spec


def peak_freq(freqs, spec, lo, hi):
    band = (freqs >= lo) & (freqs <= hi)
    idx = np.argmax(spec[band])
    k = np.where(band)[0][idx]
    f = freqs[k]
    if 0 < k < len(spec) - 1:
        a, b, c = np.log(spec[k - 1] + 1e-12), np.log(spec[k] + 1e-12), np.log(spec[k + 1] + 1e-12)
        f = freqs[k] + 0.5 * (a - c) / (a - 2 * b + c) * (freqs[1] - freqs[0])
    return f


def mag_at(freqs, spec, f):
    return spec[np.argmin(np.abs(freqs - f))]


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "synth-voice-renders"
    print(f"analyzing voice renders in {d}")

    saw = load(f"{d}/voice_saw_a440.wav")
    full = load(f"{d}/voice_full_a440.wav")
    triad = load(f"{d}/voice_triad.wav")
    low = load(f"{d}/voice_cutoff_low.wav")
    high = load(f"{d}/voice_cutoff_high.wav")

    # 1. A440 fundamental: FFT peak + zero crossings (steady window)
    freqs, spec = spectrum(saw, 0.15, 0.85)
    f0 = peak_freq(freqs, spec, 100.0, 1000.0)
    check("voice saw fundamental (FFT)", abs(f0 - 440.0) / 440.0 <= 0.01,
          f"measured {f0:.2f} Hz (target 440 +/-1%)")
    seg = saw[int(0.2 * SR):int(0.7 * SR)]
    crossings = int(np.sum((seg[:-1] < 0) & (seg[1:] >= 0)))
    zc_freq = crossings / 0.5
    check("voice saw fundamental (zero-crossing)", abs(zc_freq - 440.0) / 440.0 <= 0.02,
          f"measured {zc_freq:.1f} Hz (target 440 +/-2%)")

    # 2. saw harmonics through the wide-open filter
    h1 = mag_at(freqs, spec, 440.0)
    for n in range(2, 7):
        ratio = mag_at(freqs, spec, 440.0 * n) / h1
        expected = 1.0 / n
        check(f"voice saw H{n} ~= 1/{n}", abs(ratio - expected) / expected <= 0.30,
              f"measured ratio {ratio:.3f} vs expected {expected:.3f}")

    # 3. sustain level while the gate is held (amp env sustain 0.8)
    sus = rms_db(saw[int(0.6 * SR):int(1.1 * SR)])
    check("voice sustain RMS > -20 dBFS", sus > -20.0, f"measured {sus:.2f} dBFS in 0.6-1.1 s")

    # 4. silence after release: gate off 1.2 s, release 0.30 s ends 1.5 s;
    # check the last 0.1 s of the 2.0 s render (1.9-2.0 s).
    rel = rms_db(saw[int(1.9 * SR):int(2.0 * SR)])
    check("voice post-release RMS < -60 dBFS", rel < -60.0, f"measured {rel:.2f} dBFS in 1.9-2.0 s")

    # 5. triad fundamentals present. NOTE: the default patch detunes
    # osc 2 by +7 cents, so each note's fundamental BEATS (at 261.63 Hz
    # the beat is ~1.06 Hz and a single FFT window can sit in a null —
    # measured -17.4 dB in one window while the note is plainly
    # sounding). The design gate ("FFT shows peaks at the three
    # fundamentals") is therefore measured on power-averaged spectra
    # over the whole sustain, which is beat-phase invariant.
    def averaged_spectrum(x, starts=(0.10, 0.25, 0.40, 0.55, 0.70, 0.85), win=0.3):
        n = 1 << 14
        acc = None
        for t0 in starts:
            seg = x[int(t0 * SR):int(t0 * SR) + n]
            s = np.abs(np.fft.rfft(seg * np.hanning(n)))
            acc = s if acc is None else acc + s
        return np.fft.rfftfreq(n, 1.0 / SR), acc / len(starts)

    tfreqs, tspec = averaged_spectrum(triad)
    band = (tfreqs >= 200.0) & (tfreqs <= 500.0)
    band_max = np.max(tspec[band])
    for f_expected in (261.63, 329.63, 392.00):
        rel_db = 20.0 * np.log10(mag_at(tfreqs, tspec, f_expected) / band_max + 1e-12)
        check(f"triad component {f_expected} Hz", rel_db > -6.0,
              f"{rel_db:.1f} dB rel strongest 200-500 Hz peak, sustain-averaged (gate > -6 dB)")

    # 6. filter centroid A/B
    def centroid(x):
        fr, sp = spectrum(x, 0.15, 0.85)
        return float(np.sum(fr * sp) / (np.sum(sp) + 1e-12))
    c_low, c_high = centroid(low), centroid(high)
    check("filter centroid 200 Hz < 1/2 of 12000 Hz", c_low < 0.5 * c_high,
          f"centroids: low {c_low:.0f} Hz, high {c_high:.0f} Hz")

    # 7. full-patch headroom
    peak = float(np.max(np.abs(full)))
    check("full patch A440 peak < 1.2", peak < 1.2, f"peak {peak:.3f}")

    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)}/{len(results)} assertions passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
