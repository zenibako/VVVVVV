# pocket-music

Converts game music into packs that openfpgaOS ports on the Analogue Pocket
stream from the SD card. The Pocket's CPU can't decode Ogg Vorbis while a
game runs, so music is converted once, on a computer, to mono 4-bit IMA
ADPCM with its loop points kept.

## VVVVVV

```sh
go run github.com/zenibako/pocket-music/cmd/pocket-music@latest \
  vvvvvv vvvvvvmusic.vvv vvvvvv-music.bin
```

`vvvvvvmusic.vvv` comes with the game (on Steam, in its install folder).
Add `-mmmmmm mmmmmm.vvv` to include the MMMMMM soundtrack too. Copy
`vvvvvv-music.bin` to `Assets/vvvvvv/common/` on the card.

From a checkout, `go run ./cmd/pocket-music vvvvvv ...` does the same.

## Packages

- `adpcm`: the IMA ADPCM encoder and decoder the ports use.
- `pack`: the OFM2 pack format (see its package comment), which adds loop
  points and longer names to Butterscotch's UTM1.
- `audio`: Ogg Vorbis decoding, mixing down, resampling, and the
  `LOOPSTART`/`LOOPLENGTH`/`LOOPEND` comments SDL_mixer reads.
- `vvvvvv`: VVVVVV's music blobs, in the order the game numbers its tracks.

## Checks

```sh
go test ./...
go vet ./...
go test -C third_party/vorbis ./...
```

No game audio is kept in this repository; tests use a project-made tone
(`testdata/`).
