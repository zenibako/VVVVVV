package audio

import (
	"math"
	"os"
	"testing"
)

func TestLoop(t *testing.T) {
	cases := []struct {
		comments   []string
		start, end int64
	}{
		{nil, 0, 0},
		{[]string{"LOOPSTART=1000"}, 1000, 0},
		{[]string{"loop_start=1000", "Loop-Length=500"}, 1000, 1500},
		{[]string{"LOOPSTART=1000", "LOOPEND=4000", "LOOPLENGTH=10"}, 1000, 4000},
		{[]string{"LOOPSTART=0:01.5", "LOOPEND=1:00"}, 66150, 44100 * 60},
		{[]string{"LOOPSTART=1:02:03"}, (62*60 + 3) * 44100, 0},
		{[]string{"LOOPSTART=-5", "LOOPLENGTH=10"}, 0, 0},
		{[]string{"TITLE=Pushing Onwards", "LOOPSTART"}, 0, 0},
	}
	for _, c := range cases {
		start, end := Loop(c.comments, 44100)
		if start != c.start || end != c.end {
			t.Errorf("%q: %d-%d, want %d-%d", c.comments, start, end, c.start, c.end)
		}
	}
}

func TestResample(t *testing.T) {
	in := make([]float32, 44100)
	for i := range in {
		in[i] = float32(0.5 * math.Sin(2*math.Pi*1000*float64(i)/44100))
	}
	out, err := Resample(in, 44100, 48000)
	if err != nil {
		t.Fatal(err)
	}
	if len(out) != 48000 {
		t.Fatalf("%d samples, want 48000", len(out))
	}
	// Away from the edges it is the same tone at the new rate.
	var worst float64
	for i := 1000; i < 47000; i++ {
		want := 0.5 * math.Sin(2*math.Pi*1000*float64(i)/48000)
		worst = max(worst, math.Abs(float64(out[i])-want))
	}
	if worst > 0.01 {
		t.Errorf("worst error %.4f", worst)
	}
	if _, err := Resample(in, 0, 48000); err == nil {
		t.Error("rate 0 accepted")
	}
}

func TestDecodeOgg(t *testing.T) {
	blob, err := os.ReadFile("../testdata/tone.ogg")
	if err != nil {
		t.Fatal(err)
	}
	ogg, err := DecodeOgg(blob)
	if err != nil {
		t.Fatal(err)
	}
	if ogg.Rate != 44100 || ogg.Channels != 2 || len(ogg.Samples) != 2*22050 {
		t.Fatalf("%d Hz, %d channels, %d samples", ogg.Rate, ogg.Channels, len(ogg.Samples))
	}
	start, end := Loop(ogg.Comments, ogg.Rate)
	if start != 11025 || end != 33075 {
		t.Errorf("loop %d-%d from %q", start, end, ogg.Comments)
	}
	if len(ogg.Mono()) != 22050 {
		t.Errorf("mono has %d samples", len(ogg.Mono()))
	}
	if _, err := DecodeOgg(blob[:len(blob)/2]); err == nil {
		// A cut stream may still decode its first half; it must not crash.
		t.Log("half a stream decoded")
	}
	if _, err := DecodeOgg([]byte("OggS not really")); err == nil {
		t.Error("garbage decoded")
	}
}

func TestPCM16(t *testing.T) {
	got := PCM16([]float32{0, 1, -1, 2, -2, 0.5})
	want := []int16{0, 32767, -32767, 32767, -32767, 16384}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("%d: %d, want %d", i, got[i], want[i])
		}
	}
}
