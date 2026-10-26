package prep

import (
	"errors"
	"os"
	"path/filepath"
	"testing"
)

func TestInputRejectsLeafReplacementBeforeOpen(t *testing.T) {
	for _, kind := range []string{"symlink", "regular-file"} {
		t.Run(kind, func(t *testing.T) {
			source := t.TempDir()
			name := "data.zip"
			leaf := filepath.Join(source, name)
			other := filepath.Join(source, "other")
			if err := os.WriteFile(leaf, []byte("validated source"), 0644); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(other, []byte("unvalidated replacement"), 0644); err != nil {
				t.Fatal(err)
			}
			var opened *os.File
			data, err := inputWithOpen(source, name, false, func(path string) (*os.File, error) {
				if err := os.Rename(leaf, leaf+".original"); err != nil {
					t.Fatal(err)
				}
				if kind == "symlink" {
					if err := os.Symlink(other, leaf); err != nil {
						t.Fatal(err)
					}
				} else if err := os.Rename(other, leaf); err != nil {
					t.Fatal(err)
				}
				var err error
				opened, err = os.Open(path)
				return opened, err
			})
			if err == nil || data != nil {
				t.Fatalf("read replacement after validation: %q, %v", data, err)
			}
			var safe *Error
			if !errors.As(err, &safe) {
				t.Fatalf("unsafe error type: %T", err)
			}
			if opened == nil {
				t.Fatal("test did not reach open")
			}
			if err := opened.Close(); !errors.Is(err, os.ErrClosed) {
				t.Fatalf("opened replacement descriptor not closed: %v", err)
			}
		})
	}
}

func TestInputIdentityPreservesValidAndOptionalReads(t *testing.T) {
	source := t.TempDir()
	if err := os.WriteFile(filepath.Join(source, "data.zip"), []byte("validated"), 0644); err != nil {
		t.Fatal(err)
	}
	data, err := inputWithOpen(source, "data.zip", false, os.Open)
	if err != nil || string(data) != "validated" {
		t.Fatalf("valid input: %q %v", data, err)
	}
	opened := false
	data, err = inputWithOpen(source, "mmmmmm.vvv", true, func(string) (*os.File, error) { opened = true; return nil, os.ErrNotExist })
	if err != nil || data != nil || opened {
		t.Fatalf("optional missing input: %q %v, opened %v", data, err, opened)
	}
}
