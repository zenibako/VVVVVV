// Package testgame makes independent synthetic game inputs; it contains no game assets.
package testgame

import (
	"archive/zip"
	"bytes"
	_ "embed"
	"encoding/binary"
	"image"
	"image/png"
	"os"
	"path/filepath"
)

// tone is the project-made MIT pocket-music test tone, not purchased audio.
//
//go:embed tone.ogg
var tone []byte

// WriteSource writes deterministic synthetic data.zip and vvvvvvmusic.vvv inputs.
// Every numbered track has independently generated test audio.
func WriteSource(dir string) error {
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}
	var picture bytes.Buffer
	if err := png.Encode(&picture, image.NewRGBA(image.Rect(0, 0, 32, 32))); err != nil {
		return err
	}
	var data bytes.Buffer
	z := zip.NewWriter(&data)
	for _, name := range []string{"graphics/tiles.png", "graphics/sprites.png"} {
		f, err := z.Create(name)
		if err != nil {
			return err
		}
		if _, err = f.Write(picture.Bytes()); err != nil {
			return err
		}
	}
	if err := z.Close(); err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(dir, "data.zip"), data.Bytes(), 0644); err != nil {
		return err
	}
	names := []string{"0levelcomplete", "1pushingonwards", "2positiveforce", "3potentialforanything", "4passionforexploring", "5intermission", "6presentingvvvvvv", "7gamecomplete", "8predestinedfate", "9positiveforcereversed", "10popularpotpourri", "11pipedream", "12pressurecooker", "13pacedenergy", "14piercingthesky", "predestinedfatefinallevel"}
	blob := make([]byte, 128*60)
	for i, name := range names {
		h := blob[i*60:]
		copy(h, "data/music/"+name+".ogg")
		binary.LittleEndian.PutUint32(h[52:], uint32(len(tone)))
		h[56] = 1
	}
	for range names {
		blob = append(blob, tone...)
	}
	return os.WriteFile(filepath.Join(dir, "vvvvvvmusic.vvv"), blob, 0644)
}
