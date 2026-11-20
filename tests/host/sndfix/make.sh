#!/bin/sh
# The packed sound files the host's test plays (tests/host/packed_test.c), made once with ffmpeg and kept in the
# repository: tones whose pitch and length the test knows. Run from this folder to make them again.
#   TONE.MP3    2 s of 440 Hz, mono, 22050 Hz, 32 kbit/s (frames all one size), a title in both of MP3's tags
#   STEREO.MP3  2 s, 440 Hz left and 880 Hz right, 44100 Hz, frames of varying size (a Xing header counts them)
#   TONE.OGG    2 s, 440 Hz left and 880 Hz right, 44100 Hz, Vorbis, a title
#   MONO.OGG    1 s of 440 Hz, mono, 22050 Hz, Vorbis
#   PMP3.MP3    3 s of 1568 Hz, mono, 22050 Hz, 32 kbit/s: Loop A's audio suite plays it in the Player
#   POGG.OGG    3 s of 2093 Hz, mono, 22050 Hz, Vorbis: the same (their tones are looked for in the recording)
set -e
F="ffmpeg -hide_banner -loglevel error -y -fflags +bitexact"
$F -f lavfi -i "sine=frequency=440:duration=2:sample_rate=22050" -ac 1 -c:a libmp3lame -b:a 32k \
   -metadata title="A Tone" -id3v2_version 3 -write_id3v1 1 -flags +bitexact TONE.MP3
$F -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100" -f lavfi -i "sine=frequency=880:duration=2:sample_rate=44100" \
   -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" -map "[a]" -c:a libmp3lame -q:a 5 -flags +bitexact STEREO.MP3
$F -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100" -f lavfi -i "sine=frequency=880:duration=2:sample_rate=44100" \
   -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" -map "[a]" -c:a libvorbis -q:a 3 \
   -metadata title="Two Tones" -flags +bitexact TONE.OGG
$F -f lavfi -i "sine=frequency=440:duration=1:sample_rate=22050" -ac 1 -c:a libvorbis -q:a 2 -flags +bitexact MONO.OGG
$F -f lavfi -i "sine=frequency=1568:duration=3:sample_rate=22050" -ac 1 -c:a libmp3lame -b:a 32k -flags +bitexact PMP3.MP3
$F -f lavfi -i "sine=frequency=2093:duration=3:sample_rate=22050" -ac 1 -c:a libvorbis -q:a 2 -flags +bitexact POGG.OGG
