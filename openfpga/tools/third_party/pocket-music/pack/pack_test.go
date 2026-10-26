package pack

import (
	"bytes"
	"encoding/binary"
	"math"
	"testing"

	"github.com/zenibako/pocket-music/adpcm"
)

func samples(n int) []int16 {
	out := make([]int16, n)
	for i := range out {
		out[i] = int16(8000 * math.Sin(float64(i)/7))
	}
	return out
}

func TestRoundTrip(t *testing.T) {
	in := []Track{
		Sound("pppppp/0levelcomplete", 48000, samples(1001), 301, 801),
		Sound("pppppp/1pushingonwards", 48000, samples(200), 0, 0),
		File("notes.txt", []byte("hello")),
	}
	var buf bytes.Buffer
	if err := Write(&buf, in); err != nil {
		t.Fatal(err)
	}
	b := buf.Bytes()
	if string(b[:4]) != "OFM2" || binary.LittleEndian.Uint32(b[4:]) != 3 {
		t.Fatalf("bad header % x", b[:8])
	}
	out, err := Read(b)
	if err != nil {
		t.Fatal(err)
	}
	first := out[0]
	// An odd count and loop points are made even.
	if first.Count != 1000 || first.LoopStart != 300 || first.LoopEnd != 800 {
		t.Errorf("first track %d samples, loop %d-%d", first.Count, first.LoopStart, first.LoopEnd)
	}
	_, states := adpcm.Encode(samples(1000), 300)
	if first.LoopState != states[0] {
		t.Errorf("loop state %+v, want %+v", first.LoopState, states[0])
	}
	if out[1].LoopStart != 0 || out[1].LoopEnd != 200 {
		t.Errorf("unlooped track loops %d-%d", out[1].LoopStart, out[1].LoopEnd)
	}
	if out[2].Rate != 0 || string(out[2].Data) != "hello" {
		t.Errorf("stored file %+v", out[2])
	}
	for i := range in {
		if out[i].Name != in[i].Name || !bytes.Equal(out[i].Data, in[i].Data) {
			t.Errorf("track %d differs", i)
		}
	}
}

func TestBadLoopPlaysWhole(t *testing.T) {
	tr := Sound("x", 48000, samples(100), 80, 40)
	if tr.LoopStart != 0 || tr.LoopEnd != 100 {
		t.Errorf("loop %d-%d", tr.LoopStart, tr.LoopEnd)
	}
}

func TestRejects(t *testing.T) {
	long := string(bytes.Repeat([]byte("a"), NameLen))
	if err := Write(&bytes.Buffer{}, []Track{File(long, nil)}); err == nil {
		t.Error("a 48-byte name was accepted")
	}
	bad := Sound("x", 48000, samples(10), 0, 0)
	bad.Data = bad.Data[:2]
	if err := Write(&bytes.Buffer{}, []Track{bad}); err == nil {
		t.Error("short sound data was accepted")
	}
	if _, err := Read([]byte("UTM1\x00\x00\x00\x00")); err == nil {
		t.Error("a UTM1 pack read as OFM2")
	}
	var buf bytes.Buffer
	Write(&buf, []Track{File("a", []byte("abc"))})
	if _, err := Read(buf.Bytes()[:buf.Len()-1]); err == nil {
		t.Error("a truncated pack was accepted")
	}
}
