package prep

import (
	"archive/zip"
	"bytes"
	"io"
	"os"
	"path/filepath"
	"testing"

	"github.com/zenibako/VVVVVV/openfpga/tools/internal/testgame"
)

func embeddedSource(t *testing.T, optional bool) string {
	t.Helper()
	s := filepath.Join(t.TempDir(), "source")
	if e := testgame.WriteEmbeddedMusicSource(s, optional); e != nil {
		t.Fatal(e)
	}
	return s
}

// replaceEmbedded rewrites only synthetic test data, preserving a valid ZIP CRC
// so tests exercise music validation rather than archive checksum rejection.
func replaceEmbedded(t *testing.T, s, name string, replace func([]byte) []byte) {
	t.Helper()
	data := read(t, filepath.Join(s, "data.zip"))
	zr, e := zip.NewReader(bytes.NewReader(data), int64(len(data)))
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
		b, e := io.ReadAll(r)
		r.Close()
		if e != nil {
			t.Fatal(e)
		}
		if entry.Name == name {
			b = replace(b)
		}
		w, e := zw.Create(entry.Name)
		if e != nil {
			t.Fatal(e)
		}
		if _, e = w.Write(b); e != nil {
			t.Fatal(e)
		}
	}
	if e = zw.Close(); e != nil {
		t.Fatal(e)
	}
	if e = os.WriteFile(filepath.Join(s, "data.zip"), out.Bytes(), 0644); e != nil {
		t.Fatal(e)
	}
}

func TestPrepareEmbeddedMusicOnlyAndOptional(t *testing.T) {
	for _, optional := range []bool{false, true} {
		t.Run(map[bool]string{false: "primary", true: "optional"}[optional], func(t *testing.T) {
			s := embeddedSource(t, optional)
			original := read(t, filepath.Join(s, "data.zip"))
			out := filepath.Join(t.TempDir(), "out")
			r, e := Prepare(s, out, nil)
			if e != nil {
				t.Fatal(e)
			}
			want := 16
			if optional {
				want = 32
			}
			if r.Tracks != want {
				t.Fatal(r)
			}
			if !bytes.Equal(original, read(t, filepath.Join(out, "data.zip"))) {
				t.Fatal("changed embedded-music data.zip")
			}
			if _, e = os.Stat(filepath.Join(s, "vvvvvvmusic.vvv")); !os.IsNotExist(e) {
				t.Fatal("extracted into source")
			}
		})
	}
}

func TestExternalMusicTakesPrecedence(t *testing.T) {
	for _, name := range []string{"vvvvvvmusic.vvv", "mmmmmm.vvv"} {
		t.Run(name, func(t *testing.T) {
			external := read(t, filepath.Join(source(t), "vvvvvvmusic.vvv"))
			s := embeddedSource(t, true)
			replaceEmbedded(t, s, name, func(b []byte) []byte { return b[:len(b)-1] })
			if e := os.WriteFile(filepath.Join(s, name), external, 0644); e != nil {
				t.Fatal(e)
			}
			if _, e := Prepare(s, filepath.Join(t.TempDir(), "out"), nil); e != nil {
				t.Fatalf("did not prefer external %s: %v", name, e)
			}
		})
	}
}

func TestBadExternalDoesNotFallBackToEmbedded(t *testing.T) {
	s := embeddedSource(t, false)
	os.WriteFile(filepath.Join(s, "vvvvvvmusic.vvv"), []byte("damaged"), 0644)
	if _, e := Prepare(s, filepath.Join(t.TempDir(), "out"), nil); e == nil {
		t.Fatal("ignored damaged external music")
	}
}

func TestEmbeddedMusicTruncationRejected(t *testing.T) {
	for _, name := range []string{"vvvvvvmusic.vvv", "mmmmmm.vvv"} {
		t.Run(name, func(t *testing.T) {
			s := embeddedSource(t, true)
			replaceEmbedded(t, s, name, func(b []byte) []byte { return b[:len(b)-1] })
			if _, e := Prepare(s, filepath.Join(t.TempDir(), "out"), nil); e == nil {
				t.Fatal("accepted truncated embedded music")
			}
		})
	}
}

func TestInstallEmbeddedMusicOnly(t *testing.T) {
	root := t.TempDir()
	s := filepath.Join(root, "Assets/vvvvvv/common/source")
	if e := testgame.WriteEmbeddedMusicSource(s, false); e != nil {
		t.Fatal(e)
	}
	r, e := Install(root, false, nil)
	if e != nil {
		t.Fatal(e)
	}
	if r.Tracks != 16 {
		t.Fatal(r)
	}
	if _, e = os.Stat(filepath.Join(s, "data.zip")); e != nil {
		t.Fatal("source lost")
	}
}
