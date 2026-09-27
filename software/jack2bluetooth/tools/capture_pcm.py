#!/usr/bin/env python3
"""Receive a button-triggered PCM capture; save WAV and waveform/FFT plots."""
import argparse
from pathlib import Path
import time
import wave
import zlib


class Capture:
    def __init__(self):
        self.data = None

    def feed(self, line):
        # Logs and console prompts outside the frame are allowed.
        marker = line.find(b"@PCM_")
        if marker < 0:
            return None
        fields = line[marker:].strip().split()
        if fields[0] == b"@PCM_BEGIN":
            self.rate, self.channel, self.count = map(int, fields[1:])
            if not (0 < self.rate <= 192000 and self.channel in (0, 1) and 0 < self.count <= 1000000):
                raise ValueError("Invalid capture header")
            self.data = bytearray()
        elif fields[0] == b"@PCM_DATA" and self.data is not None:
            if len(fields) != 3 or int(fields[1]) != len(self.data):
                raise ValueError("Missing, duplicate, or damaged PCM packet; capture again")
            self.data.extend(bytes.fromhex(fields[2].decode("ascii")))
            if len(self.data) > self.count * 2:
                raise ValueError("Capture exceeds announced length")
        elif fields[0] == b"@PCM_END" and self.data is not None:
            if len(self.data) != self.count * 2 or zlib.crc32(self.data) != int(fields[1], 16):
                raise ValueError("PCM length/CRC mismatch; capture again")
            return self.rate, self.channel, bytes(self.data)
        return None


def spectrum(samples, rate):
    import numpy as np
    centered = samples - np.mean(samples)
    window = np.hanning(len(samples))
    magnitude = np.abs(np.fft.rfft(centered * window)) / window.sum()
    magnitude[1:-1 if len(samples) % 2 == 0 else None] *= 2
    dbfs = 20 * np.log10(np.maximum(magnitude, 1e-12))
    return np.fft.rfftfreq(len(samples), 1 / rate), dbfs


def save_analysis(rate, channel, data, output):
    import numpy as np
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    output.parent.mkdir(parents=True, exist_ok=True)
    wav_path = output.with_suffix(".wav")
    with wave.open(str(wav_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(data)
    samples = np.frombuffer(data, dtype="<i2").astype(float) / 32768
    freq, dbfs = spectrum(samples, rate)
    rms = np.sqrt(np.mean((samples - samples.mean()) ** 2))
    print(f"Channel {'LR'[channel]}, {len(samples)} samples, {len(samples)/rate:.3f} s")
    print(f"DC: {samples.mean():.6f} FS; AC RMS: {20*np.log10(max(rms, 1e-12)):.1f} dBFS")
    print(f"Peak: {np.max(np.abs(samples)):.6f} FS; FFT bin spacing: {rate/len(samples):.3f} Hz")
    print("Largest bins near mains frequencies (not proof of mains interference):")
    for target in (50, 60, 100, 120, 150, 180):
        candidates = np.flatnonzero(np.abs(freq - target) <= 3)
        if len(candidates):
            peak = candidates[np.argmax(dbfs[candidates])]
            print(f"  near {target:3d} Hz: {freq[peak]:7.2f} Hz, {dbfs[peak]:6.1f} dBFS")
    fig, axes = plt.subplots(3, 1, figsize=(11, 9), constrained_layout=True)
    axes[0].plot(np.arange(len(samples))/rate, samples, linewidth=0.5)
    axes[0].set(xlabel="Time (s)", ylabel="Amplitude (FS)", title="Captured ADC PCM")
    axes[1].plot(freq, dbfs, linewidth=0.7)
    axes[1].set(xlim=(0, 500), ylim=(-140, 0), xlabel="Frequency (Hz)", ylabel="Amplitude (dBFS)", title="Low-frequency detail — Hann window, DC removed")
    for hz in (50, 60, 100, 120):
        axes[1].axvline(hz, linestyle="--", alpha=0.4, label=f"{hz} Hz")
    axes[1].legend()
    axes[2].semilogx(freq[1:], dbfs[1:], linewidth=0.7)
    axes[2].set(xlim=(20, rate/2), ylim=(-140, 0), xlabel="Frequency (Hz)", ylabel="Amplitude (dBFS)", title="Full spectrum (amplitude per FFT bin, not noise density)")
    for ax in axes:
        ax.grid(True, alpha=0.25)
    fig.savefig(output.with_suffix(".png"), dpi=150)
    plt.close(fig)
    print(f"Saved {wav_path} and {output.with_suffix('.png')}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Serial port, e.g. /dev/cu.usbserial-0001")
    source.add_argument("--log", type=Path, help="Read previously saved UART output instead")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--output", type=Path, default=Path("adc_capture"))
    args = parser.parse_args()
    capture = Capture()
    result = None
    if args.log:
        with args.log.open("rb") as stream:
            for line in stream:
                result = capture.feed(line)
                if result is not None:
                    break
    else:
        import serial
        # Set control lines before opening to avoid the usual auto-reset pulse.
        stream = serial.Serial(port=None, baudrate=args.baud, timeout=1)
        stream.dtr = False
        stream.rts = False
        stream.port = args.port
        with stream:
            print("Pause music; leave ADC unmuted. Press NEXT (left) or BACK (right).", flush=True)
            deadline = time.monotonic() + args.timeout
            pending = bytearray()
            while time.monotonic() < deadline:
                pending.extend(stream.read(max(1, stream.in_waiting)))
                while b"\n" in pending:
                    line, _, remaining = pending.partition(b"\n")
                    pending = bytearray(remaining)
                    if b"PCM_CAPTURE" in line:
                        print(line.decode("utf-8", errors="replace"), flush=True)
                    result = capture.feed(line)
                    if result is not None:
                        break
                if result is not None:
                    break
    if result is None:
        raise SystemExit("No complete capture. Check firmware, source mode, ADC mute, and available heap.")
    save_analysis(*result, args.output)


if __name__ == "__main__":
    main()
