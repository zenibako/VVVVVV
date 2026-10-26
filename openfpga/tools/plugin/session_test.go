package plugin_test

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/zenibako/VVVVVV/openfpga/tools/internal/testgame"
	"github.com/zenibako/VVVVVV/openfpga/tools/plugin"
)

type menu struct {
	Name    string   `json:"name"`
	Query   string   `json:"query"`
	Choices []string `json:"choices"`
}

func readMenu(t *testing.T, data []byte, err error) menu {
	t.Helper()
	if err != nil {
		t.Fatal(err)
	}
	var envelope struct {
		Choice menu `json:"Choice"`
	}
	if err := json.Unmarshal(data, &envelope); err != nil {
		t.Fatal(err)
	}
	if envelope.Choice.Name == "" || len(envelope.Choice.Choices) == 0 {
		t.Fatalf("no offered choice: %s", data)
	}
	return envelope.Choice
}

func answer(m menu, value string) []byte {
	b, _ := json.Marshal(map[string]any{"Answer": map[string]string{"name": m.Name, "value": value}})
	return b
}

func TestHelpAndDefaultKeep(t *testing.T) {
	s := plugin.New(t.TempDir(), nil)
	b, err := s.Start()
	m := readMenu(t, b, err)
	if !strings.Contains(strings.ToLower(m.Choices[0]), "keep") {
		t.Fatalf("default is not keep: %v", m.Choices)
	}
	if len(m.Choices) != 4 {
		t.Fatalf("menu has %d choices", len(m.Choices))
	}
	b, err = s.Handle(answer(m, m.Choices[2]))
	help := readMenu(t, b, err)
	for _, fragment := range []string{"Windows", "macOS", "Linux", "data.zip", "vvvvvvmusic.vvv", "mmmmmm.vvv", "Assets/vvvvvv/common/source", "Steam"} {
		if !strings.Contains(help.Query, fragment) {
			t.Errorf("help does not explain %s", fragment)
		}
	}
	b, err = s.Handle(answer(help, help.Choices[0]))
	next := readMenu(t, b, err)
	if next.Name == m.Name || next.Name == help.Name {
		t.Fatal("choice IDs reused")
	}
	if _, err = s.Handle(answer(m, m.Choices[0])); err == nil {
		t.Fatal("accepted stale install response")
	}
	if _, err = s.Handle(answer(next, next.Choices[3])); err != nil {
		t.Fatal(err)
	}
}

func TestRejectInvalidResponsesWithoutChangingMenu(t *testing.T) {
	s := plugin.New(t.TempDir(), nil)
	b, err := s.Start()
	m := readMenu(t, b, err)
	for _, bad := range [][]byte{
		[]byte(`{`), []byte(`null`), []byte(`[]`), []byte(`{}`),
		answer(m, "invented install"), answer(menu{Name: "stale"}, m.Choices[0]),
		[]byte(`{"Answer":{"name":"x","value":"y","extra":true}}`),
		append(answer(m, m.Choices[0]), []byte(` {}`)...),
		bytes.Repeat([]byte(" "), 8193),
		[]byte(`{"Answer":null}`), []byte(`{"Kill":{},"Answer":{}}`),
		[]byte(`{"Kill":{},"Kill":{}}`),
		[]byte(`{"Answer":{"name":"x","name":"y","value":"z"}}`),
	} {
		if _, err := s.Handle(bad); err == nil {
			t.Errorf("accepted invalid response %.100s", bad)
		}
	}
	b, err = s.Handle(answer(m, m.Choices[2]))
	readMenu(t, b, err)
}

func TestKeepAndDiscardInstallOwnedFixtures(t *testing.T) {
	for _, index := range []int{0, 1} {
		t.Run([]string{"keep", "discard"}[index], func(t *testing.T) {
			root := t.TempDir()
			common := filepath.Join(root, "Assets/vvvvvv/common")
			source := filepath.Join(common, "source")
			if err := testgame.WriteSource(source); err != nil {
				t.Fatal(err)
			}
			original, err := os.ReadFile(filepath.Join(source, "data.zip"))
			if err != nil {
				t.Fatal(err)
			}
			save := filepath.Join(root, "Saves/vvvvvv/common/save.dat")
			if err := os.MkdirAll(filepath.Dir(save), 0755); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(save, []byte("player progress"), 0644); err != nil {
				t.Fatal(err)
			}
			var logs []string
			s := plugin.New(root, func(message string) { logs = append(logs, message) })
			b, err := s.Start()
			m := readMenu(t, b, err)
			b, err = s.Handle(answer(m, m.Choices[index]))
			next := readMenu(t, b, err)
			if !strings.Contains(next.Query, "Installed") {
				t.Fatalf("installation failed: %s", next.Query)
			}
			if got, err := os.ReadFile(filepath.Join(common, "data.zip")); err != nil || !bytes.Equal(got, original) {
				t.Fatalf("data.zip changed: %v", err)
			}
			if music, err := os.ReadFile(filepath.Join(common, "vvvvvv-music.bin")); err != nil || len(music) < 4 || string(music[:4]) != "OFM2" {
				t.Fatalf("music missing/invalid: %v", err)
			}
			if index == 0 {
				if got, err := os.ReadFile(filepath.Join(source, "data.zip")); err != nil || !bytes.Equal(got, original) {
					t.Fatalf("keep changed source: %v", err)
				}
			} else if _, err := os.Stat(source); !os.IsNotExist(err) {
				t.Fatalf("discard left source: %v", err)
			}
			if got, err := os.ReadFile(save); err != nil || string(got) != "player progress" {
				t.Fatalf("save changed: %v", err)
			}
			if len(logs) == 0 {
				t.Error("no progress messages")
			}
		})
	}
}

func TestInstallFailureReturnsMenuAndPreservesFiles(t *testing.T) {
	root := t.TempDir()
	source := filepath.Join(root, "Assets/vvvvvv/common/source")
	if err := os.MkdirAll(source, 0755); err != nil {
		t.Fatal(err)
	}
	file := filepath.Join(source, "data.zip")
	if err := os.WriteFile(file, []byte("invalid purchased input"), 0644); err != nil {
		t.Fatal(err)
	}
	s := plugin.New(root, nil)
	b, err := s.Start()
	m := readMenu(t, b, err)
	b, err = s.Handle(answer(m, m.Choices[1]))
	next := readMenu(t, b, err)
	if !strings.Contains(strings.ToLower(next.Query), "failed") {
		t.Errorf("failure invisible: %s", next.Query)
	}
	if next.Name == m.Name {
		t.Fatal("failed install reused choice ID")
	}
	if got, err := os.ReadFile(file); err != nil || string(got) != "invalid purchased input" {
		t.Fatalf("failed install changed source: %s %v", got, err)
	}
}

func TestKillAndExitNeverInstall(t *testing.T) {
	for _, kill := range []string{`"Kill"`, `{"Kill":{}}`, `{"Kill":null}`, ` { "Kill": { } } `} {
		t.Run(kill, func(t *testing.T) {
			root := t.TempDir()
			s := plugin.New(root, nil)
			if _, err := s.Start(); err != nil {
				t.Fatal(err)
			}
			b, err := s.Handle([]byte(kill))
			if err != nil || string(b) != `"Exit"` {
				t.Fatalf("Kill = %s, %v", b, err)
			}
			entries, _ := os.ReadDir(root)
			if len(entries) != 0 {
				t.Fatalf("cancel wrote files: %v", entries)
			}
			if _, err := s.Handle([]byte(`{}`)); err == nil {
				t.Fatal("closed session accepted response")
			}
		})
	}
	s := plugin.New(t.TempDir(), nil)
	if _, err := s.Handle([]byte(`{}`)); err == nil {
		t.Fatal("accepted response before Start")
	}
	b, err := s.Start()
	m := readMenu(t, b, err)
	b, err = s.Handle(answer(m, m.Choices[3]))
	if err != nil || string(b) != `"Exit"` {
		t.Fatalf("Exit = %s, %v", b, err)
	}
}
