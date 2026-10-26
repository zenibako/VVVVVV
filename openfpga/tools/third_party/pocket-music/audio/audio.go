// Package audio decodes Ogg Vorbis and prepares it for a pack: mixed down to
// mono, resampled and read for the loop points games keep in its comments.
package audio

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"math"
	"strconv"
	"strings"

	"github.com/jfreymuth/oggvorbis"
)

// Ogg is a decoded Ogg Vorbis stream.
type Ogg struct {
	Rate     int
	Channels int
	Samples  []float32 // interleaved
	Comments []string
}

// maxSamples bounds one decoded track: an hour of stereo 48 kHz.
const maxSamples = 48000 * 2 * 3600

// DecodeOgg decodes a whole Ogg Vorbis stream.
func DecodeOgg(blob []byte) (ogg *Ogg, err error) {
	// The patched decoder recovers its own failures; anything it missed is
	// still a bad stream, not a crash.
	defer func() {
		if recover() != nil {
			ogg, err = nil, errors.New("malformed Vorbis stream")
		}
	}()
	r, err := oggvorbis.NewReader(bytes.NewReader(blob))
	if err != nil {
		return nil, err
	}
	o := &Ogg{Rate: r.SampleRate(), Channels: r.Channels(), Comments: r.CommentHeader().Comments}
	buf := make([]float32, 4096*o.Channels)
	for {
		n, err := r.Read(buf)
		o.Samples = append(o.Samples, buf[:n]...)
		if len(o.Samples) > maxSamples {
			return nil, errors.New("track longer than an hour")
		}
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, err
		}
		if n == 0 {
			return nil, errors.New("Vorbis decoder made no progress")
		}
	}
	return o, nil
}

// Mono mixes the stream down to one channel.
func (o *Ogg) Mono() []float32 {
	frames := len(o.Samples) / o.Channels
	out := make([]float32, frames)
	for i := range out {
		var sum float32
		for c := 0; c < o.Channels; c++ {
			sum += o.Samples[i*o.Channels+c]
		}
		out[i] = sum / float32(o.Channels)
	}
	return out
}

// Loop reads the loop points SDL_mixer and VVVVVV read from Vorbis comments:
// LOOPSTART with LOOPLENGTH or LOOPEND, as sample counts or times. end is 0
// when the loop runs to the end of the track. Comments that don't make a
// valid loop give 0, 0.
func Loop(comments []string, rate int) (start, end int64) {
	var length int64
	for _, c := range comments {
		key, value, _ := strings.Cut(c, "=")
		key = strings.ToUpper(key)
		if len(key) > 4 && strings.HasPrefix(key, "LOOP") && (key[4] == '-' || key[4] == '_') {
			key = "LOOP" + key[5:]
		}
		var ok bool
		switch key {
		case "LOOPSTART":
			start, ok = parseTime(value, rate)
		case "LOOPLENGTH":
			length, ok = parseInt(value), true
		case "LOOPEND":
			end, ok = parseTime(value, rate)
		default:
			ok = true
		}
		if !ok || start < 0 || length < 0 || end < 0 {
			return 0, 0
		}
	}
	if end == 0 && length != 0 {
		end = start + length
	}
	return start, end
}

// parseInt reads a leading integer as strtoll does, 0 if there is none.
func parseInt(s string) int64 {
	s = strings.TrimLeft(s, " \t\n\v\f\r")
	end := 0
	if end < len(s) && (s[end] == '-' || s[end] == '+') {
		end++
	}
	for end < len(s) && s[end] >= '0' && s[end] <= '9' {
		end++
	}
	v, _ := strconv.ParseInt(s[:end], 10, 64)
	return v
}

// parseTime is SDL_mixer's _Mix_ParseTime: a sample count, or
// [[hh:]mm:]ss[.fraction] in seconds.
func parseTime(s string, rate int) (int64, bool) {
	if !strings.Contains(s, ":") {
		return parseInt(s), true
	}
	var result int64
	numStart := 0
	for p := 0; p < len(s); p++ {
		if s[p] == '.' || s[p] == ':' {
			v := parseInt(s[numStart:p])
			if v < 0 {
				return 0, false
			}
			result = result*60 + v
			numStart = p + 1
		}
		if s[p] == '.' {
			f, _ := strconv.ParseFloat(leadingFloat(s[p:]), 64)
			if f < 0 {
				return 0, false
			}
			return result*int64(rate) + int64(f*float64(rate)), true
		}
	}
	v := parseInt(s[numStart:])
	if v < 0 {
		return 0, false
	}
	return (result*60 + v) * int64(rate), true
}

func leadingFloat(s string) string {
	end := 0
	for end < len(s) && (s[end] == '.' || (s[end] >= '0' && s[end] <= '9')) {
		end++
	}
	return "0" + s[:end]
}

// sincTaps is the half width of the resampling filter.
const sincTaps = 16

// Resample converts mono samples from one rate to another with a windowed
// sinc filter, cut off below the lower of the two Nyquist rates.
func Resample(in []float32, from, to int) ([]float32, error) {
	if from <= 0 || to <= 0 {
		return nil, fmt.Errorf("bad sample rates %d and %d", from, to)
	}
	if from == to {
		return append([]float32(nil), in...), nil
	}
	ratio := float64(to) / float64(from)
	count := int(math.Floor(float64(len(in)) * ratio))
	cutoff := min(ratio, 1)
	out := make([]float32, count)
	for i := range out {
		x := float64(i) / ratio
		centre := int(math.Floor(x))
		var sum, weight float64
		for j := max(centre-sincTaps+1, 0); j <= min(centre+sincTaps, len(in)-1); j++ {
			t := x - float64(j)
			w := 0.5 + 0.5*math.Cos(math.Pi*t/sincTaps)
			if t != 0 {
				arg := math.Pi * t * cutoff
				w *= math.Sin(arg) / arg
			}
			sum += float64(in[j]) * w
			weight += w
		}
		if weight != 0 {
			out[i] = float32(sum / weight)
		}
	}
	return out, nil
}

// PCM16 converts samples in -1..1 to 16 bits, clipping.
func PCM16(in []float32) []int16 {
	out := make([]int16, len(in))
	for i, v := range in {
		out[i] = int16(math.Round(float64(max(min(v, 1), -1)) * 32767))
	}
	return out
}
