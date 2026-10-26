`tone.ogg` is a project-made 440 Hz test tone, half a second of stereo at
44.1 kHz with `LOOPSTART=11025` and `LOOPLENGTH=22050` comments, made with:

    ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.5" -ac 2 \
      -c:a libvorbis -q:a 2 -metadata LOOPSTART=11025 -metadata LOOPLENGTH=22050 \
      -fflags +bitexact -flags:a +bitexact tone.ogg

No game audio is kept in this repository.
