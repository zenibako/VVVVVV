package testgame

import (
	"archive/zip"
	"bytes"
	"io"
	"os"
	"path/filepath"
)

// WriteEmbeddedMusicSource models distributions which put their music blobs
// inside data.zip. Only synthetic assets are used; no external blob remains.
func WriteEmbeddedMusicSource(dir string, optional bool) error {
	if e := WriteSource(dir); e != nil {
		return e
	}
	data, e := os.ReadFile(filepath.Join(dir, "data.zip"))
	if e != nil {
		return e
	}
	music, e := os.ReadFile(filepath.Join(dir, "vvvvvvmusic.vvv"))
	if e != nil {
		return e
	}
	zr, e := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if e != nil {
		return e
	}
	var out bytes.Buffer
	zw := zip.NewWriter(&out)
	for _, entry := range zr.File {
		r, e := entry.Open()
		if e != nil {
			return e
		}
		w, e := zw.Create(entry.Name)
		if e != nil {
			r.Close()
			return e
		}
		_, e = io.Copy(w, r)
		r.Close()
		if e != nil {
			return e
		}
	}
	names := []string{"vvvvvvmusic.vvv"}
	if optional {
		names = append(names, "mmmmmm.vvv")
	}
	for _, name := range names {
		w, e := zw.Create(name)
		if e != nil {
			return e
		}
		if _, e = w.Write(music); e != nil {
			return e
		}
	}
	if e = zw.Close(); e != nil {
		return e
	}
	if e = os.WriteFile(filepath.Join(dir, "data.zip"), out.Bytes(), 0644); e != nil {
		return e
	}
	return os.Remove(filepath.Join(dir, "vvvvvvmusic.vvv"))
}
