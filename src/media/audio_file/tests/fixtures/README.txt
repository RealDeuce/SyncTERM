These fixtures are project-generated test data, BSD-2-Clause like the tests.
They are independent of the decoder under test. Source: mono signed PCM16,
44100 Hz, 4410 frames, round(8192*sin(2*pi*1000*i/44100)) for i=0..4409.

Encoded with FFmpeg (system build) from that WAV:
  ffmpeg -i tone.wav -c:a flac tone.flac
  ffmpeg -i tone.wav -c:a libvorbis -q:a 5 tone.ogg
  ffmpeg -i tone.wav -c:a libopus -b:a 96k tone.opus
  ffmpeg -i tone.wav -c:a libmp3lame -b:a 192k tone.mp3

Tests check decoded duration, stereo duplication, energy and 1 kHz frequency.
They allow lossy codec changes and encoder padding, without deriving expected
results from libsndfile or the SyncTERM decoder.
