package prep

import (
	"archive/zip"
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/zenibako/VVVVVV/openfpga/tools/internal/testgame"
	"github.com/zenibako/pocket-music/pack"
	"github.com/zenibako/pocket-music/vvvvvv"
)

func source(t *testing.T) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "source")
	if err := testgame.WriteSource(p); err != nil {
		t.Fatal(err)
	}
	return p
}
func read(t *testing.T, p string) []byte {
	t.Helper()
	b, e := os.ReadFile(p)
	if e != nil {
		t.Fatal(e)
	}
	return b
}
func TestPrepareDeterministicAndExact(t *testing.T) {
	s := source(t)
	out := filepath.Join(t.TempDir(), "out")
	r, e := Prepare(s, out, nil)
	if e != nil {
		t.Fatal(e)
	}
	if r.Tracks != 16 || len(r.Warnings) != 0 {
		t.Fatalf("report %+v", r)
	}
	if !bytes.Equal(read(t, filepath.Join(s, "data.zip")), read(t, filepath.Join(out, "data.zip"))) {
		t.Fatal("ZIP changed")
	}
	music := read(t, filepath.Join(out, "vvvvvv-music.bin"))
	tracks, e := pack.Read(music)
	if e != nil {
		t.Fatal(e)
	}
	if len(tracks) != 16 || tracks[0].Rate != 48000 || tracks[0].Count != 24000 {
		t.Fatalf("tracks %+v", tracks[0])
	}
	h := sha256.Sum256(music)
	if r.SHA256["vvvvvv-music.bin"] != hex.EncodeToString(h[:]) {
		t.Fatal("wrong hash")
	}
	var metadata Report
	if e = json.Unmarshal(read(t, filepath.Join(out, "prepare-report.json")), &metadata); e != nil {
		t.Fatal(e)
	}
	if metadata.Tracks != r.Tracks {
		t.Fatal("metadata differs")
	}
	out2 := filepath.Join(t.TempDir(), "out")
	if _, e = Prepare(s, out2, nil); e != nil {
		t.Fatal(e)
	}
	if !bytes.Equal(music, read(t, filepath.Join(out2, "vvvvvv-music.bin"))) {
		t.Fatal("nondeterministic")
	}
	if _, e = Prepare(s, out, nil); e == nil {
		t.Fatal("overwrote existing output")
	}
}
func TestPrepareRejectsBadInput(t *testing.T) {
	for _, tc := range []string{"missing", "truncated", "corrupt", "unsafe", "duplicate", "unrelated", "symlink", "symlink-parent", "huge"} {
		t.Run(tc, func(t *testing.T) {
			s := source(t)
			switch tc {
			case "missing":
				os.Remove(filepath.Join(s, "vvvvvvmusic.vvv"))
			case "truncated":
				b := read(t, filepath.Join(s, "vvvvvvmusic.vvv"))
				os.WriteFile(filepath.Join(s, "vvvvvvmusic.vvv"), b[:len(b)-1], 0644)
			case "corrupt":
				b := read(t, filepath.Join(s, "vvvvvvmusic.vvv"))
				b[128*60] = 'X'
				os.WriteFile(filepath.Join(s, "vvvvvvmusic.vvv"), b, 0644)
			case "unsafe", "duplicate", "unrelated":
				var b bytes.Buffer
				z := zip.NewWriter(&b)
				names := []string{"../escape"}
				if tc == "duplicate" {
					names = []string{"graphics/tiles.png", "graphics/tiles.png"}
				}
				if tc == "unrelated" {
					names = []string{"readme.txt"}
				}
				for _, n := range names {
					f, _ := z.Create(n)
					f.Write([]byte("x"))
				}
				z.Close()
				os.WriteFile(filepath.Join(s, "data.zip"), b.Bytes(), 0644)
			case "symlink":
				os.Remove(filepath.Join(s, "data.zip"))
				os.Symlink("/etc/passwd", filepath.Join(s, "data.zip"))
			case "symlink-parent":
				link := filepath.Join(t.TempDir(), "link")
				os.Symlink(s, link)
				s = link
			case "huge":
				f, _ := os.Create(filepath.Join(s, "data.zip"))
				f.Truncate(maxInput + 1)
				f.Close()
			}
			out := filepath.Join(t.TempDir(), "out")
			if _, e := Prepare(s, out, nil); e == nil {
				t.Fatal("accepted invalid input")
			}
			if _, e := os.Stat(out); !errors.Is(e, os.ErrNotExist) {
				t.Fatal("published failed output")
			}
		})
	}
}
func TestInstallPreservesSavesAndSource(t *testing.T) {
	for _, discard := range []bool{false, true} {
		t.Run(map[bool]string{false: "keep", true: "discard"}[discard], func(t *testing.T) {
			root := t.TempDir()
			common := filepath.Join(root, "Assets/vvvvvv/common")
			s := filepath.Join(common, "source")
			if e := testgame.WriteSource(s); e != nil {
				t.Fatal(e)
			}
			os.WriteFile(filepath.Join(common, "save.vvv"), []byte("SAVE"), 0644)
			os.WriteFile(filepath.Join(common, "data.zip"), []byte("OLD"), 0644)
			if _, e := Install(root, discard, nil); e != nil {
				t.Fatal(e)
			}
			if string(read(t, filepath.Join(common, "save.vvv"))) != "SAVE" {
				t.Fatal("save changed")
			}
			_, e := os.Stat(s)
			if discard != errors.Is(e, os.ErrNotExist) {
				t.Fatalf("discard %v, stat %v", discard, e)
			}
			entries, _ := os.ReadDir(common)
			for _, entry := range entries {
				if strings.HasPrefix(entry.Name(), ".") {
					t.Fatalf("left staging %s", entry.Name())
				}
			}
		})
	}
}
func TestInstallRollback(t *testing.T) {
	root := t.TempDir()
	common := filepath.Join(root, "Assets/vvvvvv/common")
	s := filepath.Join(common, "source")
	if e := testgame.WriteSource(s); e != nil {
		t.Fatal(e)
	}
	for _, name := range outputNames {
		os.WriteFile(filepath.Join(common, name), []byte("OLD-"+name), 0644)
	}
	calls := 0
	ops := fileOps{rename: func(a, b string) error {
		calls++
		if calls == 4 {
			return errors.New("injected replacement failure")
		}
		return os.Rename(a, b)
	}}
	if _, e := install(root, true, nil, ops); e == nil {
		t.Fatal("expected failure")
	}
	for _, name := range outputNames {
		if string(read(t, filepath.Join(common, name))) != "OLD-"+name {
			t.Fatal("rollback lost " + name)
		}
	}
	if _, e := os.Stat(s); e != nil {
		t.Fatal("source lost")
	}
}
func TestInstallRejectsBoundarySymlink(t *testing.T) {
	root := t.TempDir()
	elsewhere := t.TempDir()
	os.Symlink(elsewhere, filepath.Join(root, "Assets"))
	if _, e := Install(root, false, nil); e == nil {
		t.Fatal("followed card boundary symlink")
	}
}

func TestOptionalMusicAndPinnedParity(t *testing.T) {
	s := source(t)
	primary := read(t, filepath.Join(s, "vvvvvvmusic.vvv"))
	os.WriteFile(filepath.Join(s, "mmmmmm.vvv"), primary, 0644)
	output := filepath.Join(t.TempDir(), "out")
	r, e := Prepare(s, output, nil)
	if e != nil {
		t.Fatal(e)
	}
	if r.Tracks != 32 {
		t.Fatal(r)
	}
	tracks, e := vvvvvv.Build(primary, primary, nil, nil)
	if e != nil {
		t.Fatal(e)
	}
	var expected bytes.Buffer
	if e = pack.Write(&expected, tracks); e != nil {
		t.Fatal(e)
	}
	if !bytes.Equal(expected.Bytes(), read(t, filepath.Join(output, "vvvvvv-music.bin"))) {
		t.Fatal("pinned conversion differs")
	}
}

func TestInstallEveryRenameFailureAndCorruption(t *testing.T) {
	for fail := 1; fail <= 5; fail++ {
		t.Run(string(rune('0'+fail)), func(t *testing.T) {
			root := t.TempDir()
			common := filepath.Join(root, "Assets/vvvvvv/common")
			s := filepath.Join(common, "source")
			if e := testgame.WriteSource(s); e != nil {
				t.Fatal(e)
			}
			for _, name := range outputNames {
				os.WriteFile(filepath.Join(common, name), []byte("OLD-"+name), 0644)
			}
			os.WriteFile(filepath.Join(common, "save.vvv"), []byte("SAVE"), 0644)
			calls := 0
			ops := fileOps{rename: func(a, b string) error {
				calls++
				if fail < 5 && calls == fail {
					return errors.New("injected")
				}
				e := os.Rename(a, b)
				if e == nil && fail == 5 && calls == 4 {
					os.WriteFile(b, []byte("corruption"), 0644)
				}
				return e
			}}
			if _, e := install(root, true, nil, ops); e == nil {
				t.Fatal("accepted failed install")
			}
			for _, name := range outputNames {
				if string(read(t, filepath.Join(common, name))) != "OLD-"+name {
					t.Fatal("lost old " + name)
				}
			}
			if _, e := os.Stat(s); e != nil {
				t.Fatal("discarded source")
			}
			if string(read(t, filepath.Join(common, "save.vvv"))) != "SAVE" {
				t.Fatal("save changed")
			}
		})
	}
}

func TestCleanupFailureIsWarning(t *testing.T) {
	root := t.TempDir()
	s := filepath.Join(root, "Assets/vvvvvv/common/source")
	if e := testgame.WriteSource(s); e != nil {
		t.Fatal(e)
	}
	r, e := install(root, true, nil, fileOps{rename: os.Rename, removeAll: func(string) error { return errors.New("injected cleanup failure") }})
	if e != nil {
		t.Fatal(e)
	}
	if len(r.Warnings) != 2 {
		t.Fatalf("warnings %v", r.Warnings)
	}
	if _, e = os.Stat(s); e != nil {
		t.Fatal("source disappeared")
	}
}

func TestRollbackFailureRetainsRecovery(t *testing.T) {
	root := t.TempDir()
	common := filepath.Join(root, "Assets/vvvvvv/common")
	s := filepath.Join(common, "source")
	if e := testgame.WriteSource(s); e != nil {
		t.Fatal(e)
	}
	for _, name := range outputNames {
		os.WriteFile(filepath.Join(common, name), []byte("OLD-"+name), 0644)
	}
	calls := 0
	_, e := install(root, true, nil, fileOps{rename: func(a, b string) error {
		calls++
		if calls >= 4 {
			return errors.New("injected")
		}
		return os.Rename(a, b)
	}})
	if e == nil || !strings.Contains(e.Error(), "Recovery files remain") {
		t.Fatalf("error %v", e)
	}
	stages, _ := filepath.Glob(filepath.Join(common, ".vvvvvv-stage-*"))
	if len(stages) != 1 {
		t.Fatal("missing recovery folder")
	}
	for _, name := range outputNames {
		if string(read(t, filepath.Join(stages[0], "backup", name))) != "OLD-"+name {
			t.Fatal("lost backup")
		}
	}
	if _, e = os.Stat(s); e != nil {
		t.Fatal("discarded source")
	}
}

func TestArchiveUnsafeAndBounded(t *testing.T) {
	for _, name := range []string{"../escape", "/absolute", "C:/drive", "..\\escape", "foo//bar", "./foo", "a/../../b", "bad\x00name"} {
		t.Run(name, func(t *testing.T) {
			var b bytes.Buffer
			z := zip.NewWriter(&b)
			f, e := z.Create(name)
			if e != nil {
				t.Fatal(e)
			}
			f.Write([]byte("x"))
			z.Close()
			if e = validateZIP(b.Bytes()); e == nil {
				t.Fatal("accepted unsafe path")
			}
		})
	}
	var b bytes.Buffer
	z := zip.NewWriter(&b)
	f, e := z.Create("file")
	if e != nil {
		t.Fatal(e)
	}
	f.Write([]byte("x"))
	z.Close()
	data := b.Bytes()
	central := bytes.Index(data, []byte{'P', 'K', 1, 2})
	binary.LittleEndian.PutUint32(data[central+24:], uint32(maxArchiveEntry+1))
	if e = validateZIP(data); e == nil || !strings.Contains(e.Error(), "limit") {
		t.Fatalf("uncompressed limit: %v", e)
	}
}

func TestArchiveRejectsStandaloneParentPaths(t *testing.T) {
	valid := read(t, filepath.Join(source(t), "data.zip"))
	zr, err := zip.NewReader(bytes.NewReader(valid), int64(len(valid)))
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"..", "../"} {
		t.Run(name, func(t *testing.T) {
			var out bytes.Buffer
			zw := zip.NewWriter(&out)
			for _, entry := range zr.File {
				r, err := entry.Open()
				if err != nil {
					t.Fatal(err)
				}
				w, err := zw.Create(entry.Name)
				if err != nil {
					t.Fatal(err)
				}
				_, err = io.Copy(w, r)
				r.Close()
				if err != nil {
					t.Fatal(err)
				}
			}
			if _, err := zw.Create(name); err != nil {
				t.Fatal(err)
			}
			if err := zw.Close(); err != nil {
				t.Fatal(err)
			}
			if err := validateZIP(out.Bytes()); err == nil || !strings.Contains(err.Error(), "unsafe") {
				t.Fatalf("standalone parent path accepted: %v", err)
			}
		})
	}
}

func TestZIPRejectsTruncatedGamePNG(t *testing.T) {
	original := read(t, filepath.Join(source(t), "data.zip"))
	zr, e := zip.NewReader(bytes.NewReader(original), int64(len(original)))
	if e != nil {
		t.Fatal(e)
	}
	var out bytes.Buffer
	zw := zip.NewWriter(&out)
	for _, entry := range zr.File {
		r, e := entry.Open()
		if e != nil {
			t.Fatal(e)
		}
		data, e := io.ReadAll(r)
		r.Close()
		if e != nil {
			t.Fatal(e)
		}
		f, e := zw.Create(entry.Name)
		if e != nil {
			t.Fatal(e)
		}
		f.Write(data[:41])
	}
	zw.Close()
	if e = validateZIP(out.Bytes()); e == nil {
		t.Fatal("accepted PNG header without image data")
	}
}

func TestRemoveTreeDoesNotFollowSymlinks(t *testing.T) {
	root := filepath.Join(t.TempDir(), "dedicated")
	nested := filepath.Join(root, "nested")
	if e := os.MkdirAll(nested, 0755); e != nil {
		t.Fatal(e)
	}
	os.WriteFile(filepath.Join(nested, "file"), []byte("x"), 0644)
	outside := filepath.Join(t.TempDir(), "save")
	os.WriteFile(outside, []byte("SAVE"), 0644)
	os.Symlink(filepath.Dir(outside), filepath.Join(root, "link"))
	if e := removeTree(root); e != nil {
		t.Fatal(e)
	}
	if _, e := os.Lstat(root); !os.IsNotExist(e) {
		t.Fatal("folder survived")
	}
	if string(read(t, outside)) != "SAVE" {
		t.Fatal("followed symlink")
	}
	if e := removeTree(root); e != nil {
		t.Fatal("missing path", e)
	}
	for _, unsafe := range []string{"", ".", "/"} {
		if e := removeTree(unsafe); e == nil {
			t.Fatal("accepted unsafe cleanup root")
		}
	}
}

func TestRejectDuplicateRuntimeTrackID(t *testing.T) {
	s := source(t)
	b := read(t, filepath.Join(s, "vvvvvvmusic.vvv"))
	clear(b[60:108])
	copy(b[60:108], "music/0levelcomplete.ogg")
	os.WriteFile(filepath.Join(s, "vvvvvvmusic.vvv"), b, 0644)
	if _, e := Prepare(s, filepath.Join(t.TempDir(), "out"), nil); e == nil {
		t.Fatal("accepted duplicate runtime track ID")
	}
}
