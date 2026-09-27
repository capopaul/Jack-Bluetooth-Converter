import unittest
import zlib
import numpy as np
from capture_pcm import Capture, spectrum


class CaptureTests(unittest.TestCase):
    def test_round_trip_with_logs_and_crlf(self):
        data = bytes(range(256))
        capture = Capture()
        for line in (
            b"boot log\n", b"prompt> @PCM_BEGIN 44100 1 128\r\n",
            b"@PCM_DATA 0 " + data[:128].hex().encode() + b"\r\n",
            b"I (1000) BT: connected\n",
            b"@PCM_DATA 128 " + data[128:].hex().encode() + b"\n",
        ):
            self.assertIsNone(capture.feed(line))
        self.assertEqual(capture.feed(f"@PCM_END {zlib.crc32(data):08x}".encode()), (44100, 1, data))

    def test_missing_packet_and_corruption_rejected(self):
        capture = Capture()
        capture.feed(b"@PCM_BEGIN 44100 0 2")
        with self.assertRaises(ValueError):
            capture.feed(b"@PCM_DATA 2 0000")
        capture.feed(b"@PCM_DATA 0 00000000")
        with self.assertRaises(ValueError):
            capture.feed(b"@PCM_END 00000000")

    def test_mains_peaks_and_amplitude(self):
        rate, count = 44100, 32768
        t = np.arange(count) / rate
        for hz in (50, 60):
            freq, db = spectrum(0.1 * np.sin(2*np.pi*hz*t) + 0.02, rate)
            peak = np.argmax(db)
            self.assertLess(abs(freq[peak] - hz), rate/count)
            self.assertAlmostEqual(db[peak], -20, delta=1.5)


if __name__ == "__main__":
    unittest.main()
