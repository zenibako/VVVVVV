// Package pack writes and reads OFM2 music packs: sound converted to mono
// IMA ADPCM (package adpcm), plus files stored unchanged, for openfpgaOS
// ports to stream from the SD card.
//
// Layout, little-endian:
//
//	magic  [4]byte "OFM2"
//	count  uint32
//	count entries of 76 bytes, in the order the converter gives them
//	data
//
// An entry:
//
//	 0 name          [48]byte, NUL padded
//	48 offset        uint32, file offset of the data
//	52 count         uint32, samples (bytes for a stored file)
//	56 rate          uint32, sample rate; 0 marks a stored file
//	60 loopStart     uint32
//	64 loopEnd       uint32
//	68 loopPredictor int16
//	70 loopStep      uint8
//	71 flags         uint8, 0
//	72 reserved      uint32, 0
//
// A sound track is count/2 bytes of ADPCM. count, loopStart and loopEnd are
// even, so every point a track starts or loops from is a byte boundary. A
// looping track plays to loopEnd and carries on from loopStart with the
// decoder state loopPredictor and loopStep, the encoder's state there.
// A track that doesn't loop has loopStart 0 and loopEnd count.
//
// OFM2 follows Butterscotch's UTM1 pack, adding loop points and longer names.
package pack

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"

	"github.com/zenibako/pocket-music/adpcm"
)

const (
	Magic      = "OFM2"
	NameLen    = 48
	headerSize = 8
	entrySize  = 76
	maxTracks  = 4096
)

// Track is one entry and its data.
type Track struct {
	Name      string
	Rate      int // 0 for a stored file
	Count     int // samples, or bytes for a stored file
	LoopStart int
	LoopEnd   int
	LoopState adpcm.State
	Data      []byte
}

// Sound makes a track from mono samples at rate. loopStart and loopEnd are
// rounded down to even samples; loopEnd 0 means the end of the track.
func Sound(name string, rate int, samples []int16, loopStart, loopEnd int) Track {
	samples = samples[:len(samples)&^1]
	if loopEnd <= 0 || loopEnd > len(samples) {
		loopEnd = len(samples)
	}
	loopStart &^= 1
	loopEnd &^= 1
	if loopStart >= loopEnd {
		loopStart, loopEnd = 0, len(samples)
	}
	data, states := adpcm.Encode(samples, loopStart)
	return Track{
		Name:      name,
		Rate:      rate,
		Count:     len(samples),
		LoopStart: loopStart,
		LoopEnd:   loopEnd,
		LoopState: states[0],
		Data:      data,
	}
}

// File makes a track that stores data unchanged.
func File(name string, data []byte) Track {
	return Track{Name: name, Count: len(data), Data: data}
}

// Write writes tracks as a pack.
func Write(w io.Writer, tracks []Track) error {
	if len(tracks) > maxTracks {
		return fmt.Errorf("pack: %d tracks, at most %d", len(tracks), maxTracks)
	}
	head := make([]byte, headerSize+entrySize*len(tracks))
	copy(head, Magic)
	binary.LittleEndian.PutUint32(head[4:], uint32(len(tracks)))
	offset := uint64(len(head))
	for i, t := range tracks {
		if len(t.Name) == 0 || len(t.Name) >= NameLen {
			return fmt.Errorf("pack: name %q must be 1 to %d bytes", t.Name, NameLen-1)
		}
		if t.Rate != 0 && (t.Count&1 != 0 || len(t.Data) != t.Count/2) {
			return fmt.Errorf("pack: %s: sound data doesn't match its count", t.Name)
		}
		if t.Rate == 0 && len(t.Data) != t.Count {
			return fmt.Errorf("pack: %s: file data doesn't match its count", t.Name)
		}
		if offset+uint64(len(t.Data)) > 1<<32-1 {
			return errors.New("pack: larger than 4 GiB")
		}
		e := head[headerSize+entrySize*i:]
		copy(e, t.Name)
		binary.LittleEndian.PutUint32(e[48:], uint32(offset))
		binary.LittleEndian.PutUint32(e[52:], uint32(t.Count))
		binary.LittleEndian.PutUint32(e[56:], uint32(t.Rate))
		binary.LittleEndian.PutUint32(e[60:], uint32(t.LoopStart))
		binary.LittleEndian.PutUint32(e[64:], uint32(t.LoopEnd))
		binary.LittleEndian.PutUint16(e[68:], uint16(int16(t.LoopState.Predictor)))
		e[70] = byte(t.LoopState.StepIndex)
		offset += uint64(len(t.Data))
	}
	if _, err := w.Write(head); err != nil {
		return err
	}
	for _, t := range tracks {
		if _, err := w.Write(t.Data); err != nil {
			return err
		}
	}
	return nil
}

// Read parses a pack held in memory; each track's Data aliases b.
func Read(b []byte) ([]Track, error) {
	if len(b) < headerSize || string(b[:4]) != Magic {
		return nil, errors.New("pack: not an OFM2 pack")
	}
	n := int(binary.LittleEndian.Uint32(b[4:]))
	if n > maxTracks || headerSize+entrySize*n > len(b) {
		return nil, errors.New("pack: truncated track table")
	}
	tracks := make([]Track, n)
	for i := range tracks {
		e := b[headerSize+entrySize*i:]
		name := e[:NameLen]
		end := 0
		for end < NameLen && name[end] != 0 {
			end++
		}
		t := Track{
			Name:      string(name[:end]),
			Count:     int(binary.LittleEndian.Uint32(e[52:])),
			Rate:      int(binary.LittleEndian.Uint32(e[56:])),
			LoopStart: int(binary.LittleEndian.Uint32(e[60:])),
			LoopEnd:   int(binary.LittleEndian.Uint32(e[64:])),
			LoopState: adpcm.State{
				Predictor: int32(int16(binary.LittleEndian.Uint16(e[68:]))),
				StepIndex: int32(e[70]),
			},
		}
		size := t.Count
		if t.Rate != 0 {
			size = t.Count / 2
		}
		offset := int(binary.LittleEndian.Uint32(e[48:]))
		if offset > len(b) || size > len(b)-offset {
			return nil, fmt.Errorf("pack: %s: data past the end", t.Name)
		}
		t.Data = b[offset : offset+size]
		tracks[i] = t
	}
	return tracks, nil
}
