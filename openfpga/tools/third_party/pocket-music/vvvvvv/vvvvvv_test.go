package vvvvvv

import (
	"encoding/binary"
	"os"
	"strings"
	"testing"
)

// blob lays files out as the game's BinaryBlob writes them.
func blob(files ...Entry) []byte {
	b := make([]byte, blobHeaders*blobHeaderSize)
	for i, f := range files {
		h := b[i*blobHeaderSize:]
		copy(h, f.Name)
		binary.LittleEndian.PutUint32(h[52:], uint32(len(f.Data)))
		h[56] = 1
	}
	for _, f := range files {
		b = append(b, f.Data...)
	}
	return b
}

func tone(t *testing.T) []byte {
	b, err := os.ReadFile("../testdata/tone.ogg")
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestReadBlob(t *testing.T) {
	b := blob(Entry{"data/music/0levelcomplete.ogg", []byte("abc")}, Entry{"x", []byte("de")})
	// An invalid header in between is skipped without taking data.
	copy(b[2*blobHeaderSize:], "ignored")
	entries, err := ReadBlob(b)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 2 || string(entries[0].Data) != "abc" || entries[1].Name != "x" || string(entries[1].Data) != "de" {
		t.Errorf("entries %+v", entries)
	}
	if _, err := ReadBlob(b[:100]); err == nil {
		t.Error("a truncated blob was read")
	}
}

func TestBuildOrderAndLoops(t *testing.T) {
	ogg := tone(t)
	pppppp := blob(
		Entry{"data/extra.ogg", ogg},
		Entry{"data/music/1pushingonwards.ogg", ogg},
		Entry{"data/music/0levelcomplete.ogg", []byte("not Vorbis")},
	)
	mmmmmm := blob(Entry{"data/music/0levelcomplete.ogg", ogg})
	var warnings []string
	tracks, err := Build(pppppp, mmmmmm, nil, func(w string) { warnings = append(warnings, w) })
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	for _, tr := range tracks {
		names = append(names, tr.Name)
	}
	want := "mmmmmm/0levelcomplete pppppp/0levelcomplete pppppp/1pushingonwards pppppp/extra"
	if got := strings.Join(names, " "); got != want {
		t.Errorf("order %q, want %q", got, want)
	}
	// The broken track stays, silent, so the numbering holds.
	if tracks[1].Count != 0 {
		t.Errorf("broken track has %d samples", tracks[1].Count)
	}
	// 0.5 s at 48 kHz; LOOPSTART 11025 at 44.1 kHz, and a LOOPLENGTH that
	// runs past the end, which stops at the end.
	tr := tracks[2]
	if tr.Rate != Rate || tr.Count != 24000 || tr.LoopStart != 12000 || tr.LoopEnd != 24000 {
		t.Errorf("track %d Hz, %d samples, loop %d-%d", tr.Rate, tr.Count, tr.LoopStart, tr.LoopEnd)
	}
	joined := strings.Join(warnings, "\n")
	if !strings.Contains(joined, "pppppp/0levelcomplete is silent") || !strings.Contains(joined, "pppppp has no music/2positiveforce.ogg") {
		t.Errorf("warnings:\n%s", joined)
	}
}
