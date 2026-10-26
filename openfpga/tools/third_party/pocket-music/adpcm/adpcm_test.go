package adpcm

import (
	"math"
	"testing"
)

func tone(n int) []int16 {
	out := make([]int16, n)
	for i := range out {
		out[i] = int16(12000 * math.Sin(2*math.Pi*440*float64(i)/48000))
	}
	return out
}

func TestRoundTrip(t *testing.T) {
	in := tone(48000)
	data, _ := Encode(in)
	if len(data) != 24000 {
		t.Fatalf("%d bytes, want 24000", len(data))
	}
	out := DecodeAll(data, len(in))
	var signal, noise float64
	for i := range in {
		signal += float64(in[i]) * float64(in[i])
		d := float64(in[i]) - float64(out[i])
		noise += d * d
	}
	if snr := 10 * math.Log10(signal/noise); snr < 30 {
		t.Errorf("SNR %.1f dB, want at least 30", snr)
	}
}

// Decoding from a mark's state gives the same samples as decoding through.
func TestMarks(t *testing.T) {
	in := tone(1000)
	data, states := Encode(in, 0, 400, 1000)
	if states[0] != (State{}) {
		t.Errorf("state at 0 is %+v", states[0])
	}
	all := DecodeAll(data, len(in))
	s := states[1]
	for i := 400; i < len(in); i++ {
		b := data[i/2]
		if i&1 != 0 {
			b >>= 4
		}
		if got := s.Decode(b & 15); got != all[i] {
			t.Fatalf("sample %d: %d from the mark, %d through", i, got, all[i])
		}
	}
	if s != states[2] {
		t.Errorf("state at the end %+v, mark %+v", s, states[2])
	}
}

func TestClamps(t *testing.T) {
	in := []int16{32767, 32767, -32768, -32768, 32767, -32768}
	data, _ := Encode(in)
	out := DecodeAll(data, len(in))
	for i, v := range out {
		if v < -32768 || v > 32767 {
			t.Errorf("sample %d out of range: %d", i, v)
		}
	}
}
