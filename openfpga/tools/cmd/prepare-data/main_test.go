package main

import (
	"bytes"
	"encoding/json"
	"github.com/zenibako/VVVVVV/openfpga/tools/internal/testgame"
	"github.com/zenibako/VVVVVV/openfpga/tools/prep"
	"path/filepath"
	"strings"
	"testing"
)

func TestCLI(t *testing.T) {
	var out, stderr bytes.Buffer
	if code := run(nil, &out, &stderr); code != 2 || !strings.Contains(stderr.String(), "SOURCE OUTPUT") {
		t.Fatalf("usage %d %s", code, stderr.String())
	}
	source := filepath.Join(t.TempDir(), "source")
	if e := testgame.WriteSource(source); e != nil {
		t.Fatal(e)
	}
	output := filepath.Join(t.TempDir(), "out")
	out.Reset()
	stderr.Reset()
	if code := run([]string{source, output}, &out, &stderr); code != 0 {
		t.Fatalf("exit %d: %s", code, stderr.String())
	}
	var report prep.Report
	if e := json.Unmarshal(out.Bytes(), &report); e != nil {
		t.Fatal(e)
	}
	if report.Tracks != 16 {
		t.Fatal(report)
	}
	out.Reset()
	stderr.Reset()
	if code := run([]string{source, output}, &out, &stderr); code != 1 || out.Len() != 0 {
		t.Fatalf("existing output exit %d", code)
	}
}

func TestCLIEmbeddedMusicSource(t *testing.T) {
	source := filepath.Join(t.TempDir(), "source")
	if e := testgame.WriteEmbeddedMusicSource(source, false); e != nil {
		t.Fatal(e)
	}
	var out, stderr bytes.Buffer
	if code := run([]string{source, filepath.Join(t.TempDir(), "out")}, &out, &stderr); code != 0 {
		t.Fatalf("exit %d: %s", code, stderr.String())
	}
	var report prep.Report
	if e := json.Unmarshal(out.Bytes(), &report); e != nil {
		t.Fatal(e)
	}
	if report.Tracks != 16 {
		t.Fatal(report)
	}
}
