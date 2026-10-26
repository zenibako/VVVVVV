// Package vvvvvv converts VVVVVV's music blobs, vvvvvvmusic.vvv (PPPPPP) and
// mmmmmm.vvv (MMMMMM), into one pack for the Pocket port.
//
// Tracks are named "pppppp/<id>" and "mmmmmm/<id>", where <id> is the
// game's music id, and stored in the order the game numbers them
// (musicclass::init): MMMMMM's when its blob is given, then PPPPPP's, each
// blob's sixteen named tracks before its extras. A track the game would
// fail to open is kept, silent, so the ones after it keep their numbers.
package vvvvvv

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"strings"

	"github.com/zenibako/pocket-music/audio"
	"github.com/zenibako/pocket-music/pack"
)

// Rate is the Pocket's output rate, which the port plays music at.
const Rate = 48000

// The game's BinaryBlob: 128 headers of {name [48]byte, start int32,
// size int32, valid uint8}, padded to 60 bytes, then each valid header's
// data back to back.
const (
	blobHeaders    = 128
	blobHeaderSize = 60
)

// TrackNames is BinaryBlob.h's TRACK_NAMES.
var TrackNames = []string{
	"music/0levelcomplete.ogg",
	"music/1pushingonwards.ogg",
	"music/2positiveforce.ogg",
	"music/3potentialforanything.ogg",
	"music/4passionforexploring.ogg",
	"music/5intermission.ogg",
	"music/6presentingvvvvvv.ogg",
	"music/7gamecomplete.ogg",
	"music/8predestinedfate.ogg",
	"music/9positiveforcereversed.ogg",
	"music/10popularpotpourri.ogg",
	"music/11pipedream.ogg",
	"music/12pressurecooker.ogg",
	"music/13pacedenergy.ogg",
	"music/14piercingthesky.ogg",
	"music/predestinedfatefinallevel.ogg",
}

// Entry is one file in a blob.
type Entry struct {
	Name string
	Data []byte
}

// ReadBlob reads a blob as FILESYSTEM_loadBinaryBlob does.
func ReadBlob(b []byte) ([]Entry, error) {
	if len(b) < blobHeaders*blobHeaderSize {
		return nil, errors.New("not a VVVVVV music blob")
	}
	var entries []Entry
	offset := blobHeaders * blobHeaderSize
	for i := 0; i < blobHeaders; i++ {
		h := b[i*blobHeaderSize:]
		size := int32(binary.LittleEndian.Uint32(h[52:]))
		if h[56] != 1 || size < 1 {
			continue
		}
		name := h[:47]
		if n := strings.IndexByte(string(name), 0); n >= 0 {
			name = name[:n]
		}
		end := min(offset+int(size), len(b))
		entries = append(entries, Entry{Name: string(name), Data: b[offset:end]})
		offset = end
	}
	if len(entries) == 0 {
		return nil, errors.New("music blob has no tracks")
	}
	return entries, nil
}

// id is the music id add_builtin_track makes from a blob name.
func id(name string) string {
	name = strings.TrimPrefix(name, "data/")
	name = strings.TrimPrefix(name, "music/")
	if dot := strings.LastIndexByte(name, '.'); dot >= 0 {
		name = name[:dot]
	}
	return name
}

// ordered gives a blob's entries in the game's order.
func ordered(entries []Entry) []Entry {
	named := map[string]bool{}
	var out []Entry
	for _, track := range TrackNames {
		named["data/"+track] = true
		for _, e := range entries {
			if e.Name == "data/"+track {
				out = append(out, e)
				break
			}
		}
	}
	for _, e := range entries {
		if !named[e.Name] {
			out = append(out, e)
		}
	}
	return out
}

// Convert makes one track from an Ogg entry.
func Convert(set string, e Entry) (pack.Track, error) {
	name := set + "/" + id(e.Name)
	if len(name) >= pack.NameLen {
		return pack.Track{}, fmt.Errorf("%s: name too long", e.Name)
	}
	ogg, err := audio.DecodeOgg(e.Data)
	if err != nil {
		// Silent, as the game would play it.
		return pack.Sound(name, Rate, nil, 0, 0), err
	}
	start, end := audio.Loop(ogg.Comments, ogg.Rate)
	mono, err := audio.Resample(ogg.Mono(), ogg.Rate, Rate)
	if err != nil {
		return pack.Sound(name, Rate, nil, 0, 0), err
	}
	scale := float64(Rate) / float64(ogg.Rate)
	loopStart := int(math.Round(float64(start) * scale))
	loopEnd := int(math.Round(float64(end) * scale))
	return pack.Sound(name, Rate, audio.PCM16(mono), loopStart, loopEnd), nil
}

// Build converts the blobs into tracks. mmmmmm may be nil. warn hears of
// tracks that are missing or can't be decoded; progress, of each track as
// it starts.
func Build(pppppp, mmmmmm []byte, progress, warn func(string)) ([]pack.Track, error) {
	var tracks []pack.Track
	add := func(set string, blob []byte) error {
		entries, err := ReadBlob(blob)
		if err != nil {
			return fmt.Errorf("%s: %w", set, err)
		}
		for _, track := range TrackNames {
			found := false
			for _, e := range entries {
				found = found || e.Name == "data/"+track
			}
			if !found && warn != nil {
				warn(fmt.Sprintf("%s has no %s", set, track))
			}
		}
		for _, e := range ordered(entries) {
			if progress != nil {
				progress(set + "/" + id(e.Name))
			}
			t, err := Convert(set, e)
			if err != nil && warn != nil {
				warn(fmt.Sprintf("%s/%s is silent: %v", set, id(e.Name), err))
			}
			if t.Name == "" {
				return fmt.Errorf("%s: %w", e.Name, err)
			}
			tracks = append(tracks, t)
		}
		return nil
	}
	if mmmmmm != nil {
		if err := add("mmmmmm", mmmmmm); err != nil {
			return nil, err
		}
	}
	if err := add("pppppp", pppppp); err != nil {
		return nil, err
	}
	return tracks, nil
}
