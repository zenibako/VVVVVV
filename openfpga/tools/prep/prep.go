// Package prep validates owned VVVVVV inputs and prepares the Pocket port's assets.
package prep

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"image/png"
	"io"
	"os"
	"path"
	"path/filepath"
	"strings"

	"github.com/jfreymuth/oggvorbis"
	"github.com/zenibako/pocket-music/pack"
	"github.com/zenibako/pocket-music/vvvvvv"
)

const maxInput int64 = 256 << 20
const maxArchiveTotal uint64 = 512 << 20
const maxArchiveEntry uint64 = 64 << 20

var outputNames = []string{"data.zip", "vvvvvv-music.bin"}

// Report describes verified outputs. SHA256 keys are output basenames.
type Report struct {
	Tracks   int               `json:"tracks"`
	Warnings []string          `json:"warnings"`
	SHA256   map[string]string `json:"sha256"`
}

// Error is suitable for a plugin dialog: no host paths or decoder internals leak.
type Error struct{ Message string }

func (e *Error) Error() string                 { return e.Message }
func problem(format string, args ...any) error { return &Error{fmt.Sprintf(format, args...)} }

// safePath rejects symlinks and non-directories in every existing parent. The
// two standard macOS filesystem aliases are accepted, but no user alias is.
func safePath(name string, directory bool) error {
	abs, err := filepath.Abs(name)
	if err != nil {
		return problem("Invalid filesystem path.")
	}
	volume := filepath.VolumeName(abs)
	parts := strings.Split(strings.TrimPrefix(filepath.Clean(abs), volume), string(filepath.Separator))
	cur := volume + string(filepath.Separator)
	for i, part := range parts {
		if part == "" {
			continue
		}
		cur = filepath.Join(cur, part)
		info, err := os.Lstat(cur)
		if err != nil {
			return problem("Required folder or file is missing or inaccessible.")
		}
		if info.Mode()&os.ModeSymlink != 0 {
			target, _ := os.Readlink(cur)
			if (cur == "/var" && target == "private/var") || (cur == "/tmp" && target == "private/tmp") {
				continue
			}
			return problem("Symbolic links are not supported.")
		}
		if (i < len(parts)-1 || directory) && !info.IsDir() {
			return problem("Expected a regular folder.")
		}
		if i == len(parts)-1 && !directory && !info.Mode().IsRegular() {
			return problem("Expected a regular file.")
		}
	}
	return nil
}

func input(source, name string, optional bool) ([]byte, error) {
	return inputWithOpen(source, name, optional, os.Open)
}

func inputWithOpen(source, name string, optional bool, open func(string) (*os.File, error)) ([]byte, error) {
	p := filepath.Join(source, name)
	if optional {
		if _, e := os.Lstat(p); os.IsNotExist(e) {
			return nil, nil
		}
	}
	if err := safePath(p, false); err != nil {
		return nil, problem("%s must be a readable regular file without symbolic links.", name)
	}
	validated, e := os.Lstat(p)
	if e != nil || !validated.Mode().IsRegular() {
		return nil, problem("%s must be a readable regular file without symbolic links.", name)
	}
	// Windows resolves and caches a Lstat result's file ID lazily in SameFile.
	// Materialize that identity before opening, rather than after a replacement.
	if !os.SameFile(validated, validated) {
		return nil, problem("Cannot establish the identity of %s.", name)
	}
	f, e := open(p)
	if e != nil {
		return nil, problem("Cannot read %s.", name)
	}
	defer f.Close()
	st, e := f.Stat()
	if e != nil || !st.Mode().IsRegular() || st.Size() > maxInput {
		return nil, problem("%s exceeds the input limit or is not regular.", name)
	}
	// Bind this read to the leaf validated immediately before opening. This
	// detects replacement with another regular file or a symlink to one; it
	// does not make concurrent parent-directory changes or in-place writes safe.
	if !os.SameFile(validated, st) {
		return nil, problem("%s changed while being opened; retry with a stable source file.", name)
	}
	b, e := io.ReadAll(io.LimitReader(f, maxInput+1))
	if e != nil || int64(len(b)) > maxInput {
		return nil, problem("Cannot read %s within the input limit.", name)
	}
	return b, nil
}

// musicInput supports releases which ship blobs inside data.zip. An explicitly
// provided external file always wins; invalid external files are never silently
// replaced by embedded data. Only an exact, validated root entry is read, and
// no archive paths are extracted onto the filesystem.
func musicInput(source, name string, archive []byte, required bool) ([]byte, error) {
	if external, e := input(source, name, true); e != nil {
		return nil, e
	} else if external != nil {
		return external, nil
	}
	z, e := zip.NewReader(bytes.NewReader(archive), int64(len(archive)))
	if e != nil {
		return nil, problem("data.zip is not a valid ZIP archive.")
	}
	for _, entry := range z.File {
		if entry.Name != name {
			continue
		}
		if !entry.Mode().IsRegular() || entry.UncompressedSize64 > maxArchiveEntry {
			return nil, problem("Embedded %s is not a regular file within the input limit.", name)
		}
		r, e := entry.Open()
		if e != nil {
			return nil, problem("Cannot read embedded %s.", name)
		}
		b, e := io.ReadAll(io.LimitReader(r, int64(maxArchiveEntry)+1))
		r.Close()
		if e != nil || uint64(len(b)) != entry.UncompressedSize64 || uint64(len(b)) > maxArchiveEntry {
			return nil, problem("Embedded %s is damaged or exceeds the input limit.", name)
		}
		return b, nil
	}
	if required {
		return nil, problem("%s must be inside data.zip at its root or provided beside data.zip.", name)
	}
	return nil, nil
}

func validateZIP(b []byte) error {
	z, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		return problem("data.zip is not a valid ZIP archive.")
	}
	if len(z.File) > 4096 {
		return problem("data.zip contains too many entries.")
	}
	seen := map[string]bool{}
	required := map[string]bool{"graphics/tiles.png": false, "graphics/sprites.png": false}
	var total uint64
	for _, f := range z.File {
		n := f.Name
		clean := strings.TrimSuffix(n, "/")
		key := strings.ToLower(clean)
		if n == "" || strings.ContainsAny(n, "\\:\x00") || strings.HasPrefix(n, "/") || path.Clean(clean) != clean || clean == "." || clean == ".." || strings.HasPrefix(clean, "../") || seen[key] {
			return problem("data.zip contains an unsafe or duplicate path.")
		}
		seen[key] = true
		if f.Mode()&os.ModeSymlink != 0 || (!f.FileInfo().IsDir() && !f.Mode().IsRegular()) {
			return problem("data.zip contains a non-regular entry.")
		}
		if f.UncompressedSize64 > maxArchiveEntry || f.UncompressedSize64 > maxArchiveTotal-total {
			return problem("data.zip exceeds the extraction limit.")
		}
		total += f.UncompressedSize64
		r, e := f.Open()
		if e != nil {
			return problem("data.zip has an unreadable entry.")
		}
		if _, ok := required[n]; ok {
			// These actual game assets distinguish a game ZIP from an unrelated ZIP.
			asset, e := io.ReadAll(io.LimitReader(r, int64(maxArchiveEntry)+1))
			r.Close()
			if e != nil || uint64(len(asset)) != f.UncompressedSize64 {
				return problem("data.zip has a damaged game asset.")
			}
			cfg, e := png.DecodeConfig(bytes.NewReader(asset))
			if e != nil || cfg.Width <= 0 || cfg.Height <= 0 || cfg.Width > 4096 || cfg.Height > 4096 {
				return problem("data.zip has an invalid game image.")
			}
			if _, e = png.Decode(bytes.NewReader(asset)); e != nil {
				return problem("data.zip has a damaged game image.")
			}
			required[n] = true
		} else {
			count, e := io.Copy(io.Discard, io.LimitReader(r, int64(maxArchiveEntry)+1))
			r.Close()
			if e != nil || uint64(count) != f.UncompressedSize64 {
				return problem("data.zip has a damaged entry.")
			}
		}
	}
	for name, present := range required {
		if !present {
			return problem("data.zip is missing required game asset %s.", name)
		}
	}
	return nil
}

// The pinned reader intentionally tolerates some bad tracks for playback. A
// preparation tool must reject truncated blobs and streams before calling it.
func validateBlob(b []byte, name string) error {
	if len(b) < 128*60 {
		return problem("%s has a truncated music header.", name)
	}
	offset := 128 * 60
	count := 0
	seen := map[string]bool{}
	for i := 0; i < 128; i++ {
		h := b[i*60:]
		if h[56] == 0 {
			continue
		}
		if h[56] != 1 {
			return problem("%s has an invalid track header.", name)
		}
		size := uint64(binary.LittleEndian.Uint32(h[52:]))
		if size == 0 || size > 64<<20 || size > uint64(len(b)-offset) {
			return problem("%s has truncated or oversized track data.", name)
		}
		end := bytes.IndexByte(h[:48], 0)
		if end < 1 || end >= 48 {
			return problem("%s has an invalid track name.", name)
		}
		track := string(h[:end])
		if seen[track] || strings.ContainsAny(track, "\x00\\") || path.Clean(track) != track || strings.HasPrefix(track, "/") || strings.HasPrefix(track, "../") {
			return problem("%s has an invalid or duplicate track name.", name)
		}
		seen[track] = true
		data := b[offset : offset+int(size)]
		offset += int(size)
		count++
		if err := validateOgg(data); err != nil {
			return problem("%s contains a damaged or unsupported music track.", name)
		}
	}
	if count == 0 || offset != len(b) {
		return problem("%s has no tracks or unexpected trailing data.", name)
	}
	return nil
}

func validateOgg(b []byte) (err error) {
	defer func() {
		if recover() != nil {
			err = problem("Malformed music stream.")
		}
	}()
	if e := validateOggPages(b); e != nil {
		return e
	}
	r, e := oggvorbis.NewReader(bytes.NewReader(b))
	if e != nil {
		return e
	}
	if r.Channels() < 1 || r.Channels() > 2 || r.SampleRate() < 8000 || r.SampleRate() > 192000 || r.Length() < 1 || r.Length() > int64(r.SampleRate())*20*60 {
		return problem("Unsupported music duration, rate or channels.")
	}
	return nil
}

// Bound work before entering the pinned decoder: its packet reader recurses
// across pages with no completed packet. A 1 MiB packet and at most 64 such
// pages accommodate ordinary Vorbis headers/audio while bounding both buffered
// packet memory and WASM stack use. Chained/multiplexed streams are unsupported.
const maxOggPacketBytes = 1 << 20
const maxOggPagesWithoutPacket = 64

func validateOggPages(b []byte) error {
	// Require complete Ogg pages and an explicit stream end; a decoder can
	// otherwise turn a truncated final page into an apparently successful EOF.
	offset := 0
	ended := false
	packetBytes, pagesWithoutPacket := 0, 0
	var serial, sequence uint32
	for offset < len(b) {
		if len(b)-offset < 27 || string(b[offset:offset+4]) != "OggS" || b[offset+4] != 0 || ended {
			return problem("Invalid Ogg page.")
		}
		n := int(b[offset+26])
		if n == 0 || len(b)-offset < 27+n {
			return problem("Truncated Ogg lacing table.")
		}
		flags := b[offset+5]
		if flags&^byte(7) != 0 || (flags&1 != 0) != (packetBytes > 0) {
			return problem("Invalid Ogg packet continuation.")
		}
		pageSerial := binary.LittleEndian.Uint32(b[offset+14:])
		pageSequence := binary.LittleEndian.Uint32(b[offset+18:])
		if offset == 0 {
			serial = pageSerial
			if flags&2 == 0 || pageSequence != 0 {
				return problem("Invalid Ogg stream beginning.")
			}
		} else if flags&2 != 0 || pageSerial != serial || pageSequence != sequence {
			return problem("Chained or out-of-order Ogg pages are unsupported.")
		}
		sequence = pageSequence + 1
		size := 27 + n
		completed := 0
		for _, v := range b[offset+27 : offset+27+n] {
			size += int(v)
			packetBytes += int(v)
			if packetBytes > maxOggPacketBytes {
				return problem("Ogg packet exceeds the 1 MiB preparation limit.")
			}
			if v < 255 {
				completed++
				packetBytes = 0
			}
		}
		if size > len(b)-offset {
			return problem("Truncated Ogg page.")
		}
		if completed == 0 {
			pagesWithoutPacket++
		} else {
			pagesWithoutPacket = 0
		}
		if pagesWithoutPacket > maxOggPagesWithoutPacket {
			return problem("Ogg stream has too many pages without a completed packet (limit 64).")
		}
		ended = b[offset+5]&4 != 0
		if ended && packetBytes > 0 {
			return problem("Ogg stream ends with an incomplete packet.")
		}
		offset += size
	}
	if !ended {
		return problem("Missing Ogg end page.")
	}
	return nil
}

func writeFile(p string, b []byte) error {
	if e := os.WriteFile(p, b, 0644); e != nil {
		return problem("Cannot write prepared output.")
	}
	return nil
}
func hashFile(p string) (string, error) {
	f, e := os.Open(p)
	if e != nil {
		return "", problem("Cannot verify prepared output.")
	}
	defer f.Close()
	h := sha256.New()
	if _, e = io.Copy(h, f); e != nil {
		return "", problem("Cannot verify prepared output.")
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}
func verify(dir string, r Report) error {
	for _, name := range outputNames {
		if e := safePath(filepath.Join(dir, name), false); e != nil {
			return e
		}
		h, e := hashFile(filepath.Join(dir, name))
		if e != nil {
			return e
		}
		if h != r.SHA256[name] {
			return problem("Prepared output verification failed.")
		}
	}
	return nil
}

func prepareInto(source, stage string, progress func(string)) (Report, error) {
	report := Report{Warnings: []string{}, SHA256: map[string]string{}}
	if err := safePath(source, true); err != nil {
		return report, err
	}
	if progress != nil {
		progress("Validating owned game files")
	}
	data, e := input(source, "data.zip", false)
	if e != nil {
		return report, e
	}
	if e = validateZIP(data); e != nil {
		return report, e
	}
	primary, e := musicInput(source, "vvvvvvmusic.vvv", data, true)
	if e != nil {
		return report, e
	}
	if e = validateBlob(primary, "vvvvvvmusic.vvv"); e != nil {
		return report, e
	}
	extra, e := musicInput(source, "mmmmmm.vvv", data, false)
	if e != nil {
		return report, e
	}
	if extra != nil {
		if e = validateBlob(extra, "mmmmmm.vvv"); e != nil {
			return report, e
		}
	}
	var damaged bool
	tracks, e := vvvvvv.Build(primary, extra, progress, func(w string) {
		if strings.Contains(w, "is silent:") {
			damaged = true
		}
		report.Warnings = append(report.Warnings, w)
	})
	if e != nil || damaged {
		return report, problem("Music conversion failed; a music track is damaged.")
	}
	seenTracks := map[string]bool{}
	for _, track := range tracks {
		if seenTracks[track.Name] || track.Rate != vvvvvv.Rate || track.Count == 0 {
			return report, problem("Music contains duplicate, empty or unsupported runtime tracks.")
		}
		seenTracks[track.Name] = true
	}
	var packed bytes.Buffer
	if e = pack.Write(&packed, tracks); e != nil {
		return report, problem("Cannot encode music pack.")
	}
	if e = writeFile(filepath.Join(stage, "data.zip"), data); e != nil {
		return report, e
	}
	if e = writeFile(filepath.Join(stage, "vvvvvv-music.bin"), packed.Bytes()); e != nil {
		return report, e
	}
	report.Tracks = len(tracks)
	for _, name := range outputNames {
		h, e := hashFile(filepath.Join(stage, name))
		if e != nil {
			return report, e
		}
		report.SHA256[name] = h
	}
	return report, verify(stage, report)
}

// Prepare creates a new output directory only after all inputs and outputs are
// verified. It refuses an existing output, including an empty directory.
func Prepare(source, output string, progress func(string)) (Report, error) {
	var report Report
	if _, e := os.Lstat(output); !os.IsNotExist(e) {
		return report, problem("Output already exists or is inaccessible; choose a new folder.")
	}
	parent := filepath.Dir(filepath.Clean(output))
	if e := safePath(parent, true); e != nil {
		return report, e
	}
	stage, e := os.MkdirTemp(parent, ".vvvvvv-prep-")
	if e != nil {
		return report, problem("Cannot create staging folder.")
	}
	defer removeTree(stage)
	report, e = prepareInto(source, stage, progress)
	if e != nil {
		return report, e
	}
	metadata, e := json.MarshalIndent(report, "", "  ")
	if e != nil {
		return report, problem("Cannot encode output report.")
	}
	if e = writeFile(filepath.Join(stage, "prepare-report.json"), append(metadata, '\n')); e != nil {
		return report, e
	}
	if _, e = os.Lstat(output); !os.IsNotExist(e) {
		return report, problem("Output appeared while preparing; nothing was replaced.")
	}
	if e = os.Rename(stage, output); e != nil {
		return report, problem("Cannot publish prepared folder.")
	}
	return report, nil
}
