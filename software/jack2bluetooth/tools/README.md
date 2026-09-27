# ADC noise capture

Build and flash the firmware using your usual ESP-IDF environment. In Jack → Bluetooth mode, leave the ADC **unmuted** and pause music on the connected source. ENTER retains its existing mute toggle. NEXT captures left PCM; BACK captures right PCM.

On the PC:

```sh
python3 -m venv .venv
.venv/bin/pip install -r software/jack2bluetooth/tools/requirements.txt
.venv/bin/python software/jack2bluetooth/tools/capture_pcm.py --port /dev/cu.usbserial-0001 --output captures/paused-left
```

Replace the serial port with your board's port. Close `idf.py monitor` or any other serial terminal first. Start the script, then press NEXT or BACK once. Opening some serial adapters may reset the board; wait for normal startup before pressing the button. The script writes a mono 16-bit WAV and a PNG with waveform, 0–500 Hz detail, and full spectrum. It also reports DC, AC RMS, and peaks near 50/60 Hz and their harmonics. Frequencies assume the configured 44.1 kHz I2S rate.

The RX task copies one channel **before** the Bluetooth stream buffer. It keeps capturing normally while a separate task sends the completed snapshot. Default capture: 32,768 samples, 65,536 bytes of temporary heap, 0.743 seconds, 1.346 Hz FFT bin spacing. Allocation failure is logged rather than crashing. In `main/drivers/pcm_capture.c`, change `CAPTURE_SAMPLES` to `88200` for two seconds only if at least 176,400 bytes of contiguous heap remain alongside Bluetooth. The capture task also needs a 3 KiB stack. A stalled capture times out after five seconds.

Transport uses offset-numbered hex lines and an overall CRC32. This costs about 13 seconds at 115,200 baud for the default capture, but tolerates ordinary logs between packets and avoids console newline translation of raw binary. Missing/corrupted packets are rejected; repeat the capture if that happens. Do not press other buttons or change direction during transfer. You can also save complete UART output and analyze it with `--log uart.log` instead of `--port`.

The FFT removes the mean and applies a Hann window. Its amplitude scale is peak dBFS per bin (0 dBFS for a bin-centered full-scale sine), **not** a power spectral density. The plotted floor is -140 dBFS. Look for a narrow 50 or 60 Hz peak and harmonics versus broadband hiss; a peak alone does not prove its electrical source. Compare captures with the same gain and length. The capture uses the existing upper-16-bit I2S conversion, so it does not independently validate that conversion or measure DMA overruns. Heavy system load can still affect continuity.
