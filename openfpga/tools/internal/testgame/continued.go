package testgame

import (
	"encoding/binary"
	"os"
	"path/filepath"
)

// ContinuedPacketOgg creates CRC-valid pages which never finish their first
// packet, followed by the project's valid synthetic tone. It exercises decoder
// recursion bounds without relying on damaged checksums or truncated pages.
func ContinuedPacketOgg(pages int) []byte {
	return continuedPacketOgg(pages, 1)
}

func continuedPacketOgg(pages, segments int) []byte {
	var out []byte
	for i := 0; i < pages; i++ {
		page := make([]byte, 27+segments+segments*255)
		copy(page, "OggS")
		page[5] = 1
		if i == 0 {
			page[5] = 2
		}
		binary.LittleEndian.PutUint64(page[6:], ^uint64(0))
		binary.LittleEndian.PutUint32(page[14:], 1)
		binary.LittleEndian.PutUint32(page[18:], uint32(i))
		page[26] = byte(segments)
		for j := 0; j < segments; j++ {
			page[27+j] = 255
		}
		var crc uint32
		for _, b := range page {
			crc ^= uint32(b) << 24
			for bit := 0; bit < 8; bit++ {
				if crc&0x80000000 != 0 {
					crc = (crc << 1) ^ 0x04c11db7
				} else {
					crc <<= 1
				}
			}
		}
		binary.LittleEndian.PutUint32(page[22:], crc)
		out = append(out, page...)
	}
	return append(out, tone...)
}

// OversizedPacketOgg reaches the packet-byte limit with full lacing tables,
// independently of the consecutive-page recursion limit.
func OversizedPacketOgg() []byte { return continuedPacketOgg(17, 255) }

// WriteContinuedPacketSource creates otherwise valid game assets with one
// malformed continuation-stream track. No purchased audio is used.
func WriteContinuedPacketSource(dir string, pages int) error {
	if e := WriteSource(dir); e != nil {
		return e
	}
	data := ContinuedPacketOgg(pages)
	blob := make([]byte, 128*60)
	copy(blob, "data/music/0levelcomplete.ogg")
	binary.LittleEndian.PutUint32(blob[52:], uint32(len(data)))
	blob[56] = 1
	return os.WriteFile(filepath.Join(dir, "vvvvvvmusic.vvv"), append(blob, data...), 0644)
}
