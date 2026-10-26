// Package adpcm encodes and decodes the 4-bit IMA ADPCM the openfpgaOS ports
// stream: one channel, low nibble first, starting from predictor 0 and step
// index 0, with no block headers.
package adpcm

// State is the decoder's state between samples.
type State struct {
	Predictor int32
	StepIndex int32
}

var indexTable = [16]int32{-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8}

var stepTable = [89]int32{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
	253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
	1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
	3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
	11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767,
}

// Decode applies one 4-bit code and returns the new sample.
func (s *State) Decode(code byte) int16 {
	step := stepTable[s.StepIndex]
	diff := step >> 3
	if code&4 != 0 {
		diff += step
	}
	if code&2 != 0 {
		diff += step >> 1
	}
	if code&1 != 0 {
		diff += step >> 2
	}
	if code&8 != 0 {
		diff = -diff
	}
	s.Predictor = min(max(s.Predictor+diff, -32768), 32767)
	s.StepIndex = min(max(s.StepIndex+indexTable[code&15], 0), 88)
	return int16(s.Predictor)
}

// encode picks the code that brings the decoder closest to sample and
// decodes it, so s follows what the device will hear.
func (s *State) encode(sample int16) byte {
	step := stepTable[s.StepIndex]
	diff := int32(sample) - s.Predictor
	var code byte
	if diff < 0 {
		code = 8
		diff = -diff
	}
	part := step
	for bit := byte(4); bit != 0; bit >>= 1 {
		if diff >= part {
			code |= bit
			diff -= part
		}
		part >>= 1
	}
	s.Decode(code)
	return code
}

// Encode encodes samples, which must be an even number of them, and returns
// the decoder state at each of marks: the state before that sample is
// decoded, from which playback can start again there.
func Encode(samples []int16, marks ...int) ([]byte, []State) {
	out := make([]byte, (len(samples)+1)/2)
	states := make([]State, len(marks))
	var s State
	for i, v := range samples {
		for m, at := range marks {
			if at == i {
				states[m] = s
			}
		}
		code := s.encode(v)
		if i&1 != 0 {
			out[i/2] |= code << 4
		} else {
			out[i/2] = code
		}
	}
	for m, at := range marks {
		if at >= len(samples) {
			states[m] = s
		}
	}
	return out, states
}

// DecodeAll decodes count samples of data from the start.
func DecodeAll(data []byte, count int) []int16 {
	out := make([]int16, count)
	var s State
	for i := range out {
		b := data[i/2]
		if i&1 != 0 {
			b >>= 4
		}
		out[i] = s.Decode(b & 15)
	}
	return out
}
