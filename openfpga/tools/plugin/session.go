// Package plugin implements Pocket Sync's JSON conversation independently of its host.
package plugin

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"sync/atomic"

	"github.com/zenibako/VVVVVV/openfpga/tools/prep"
)

const (
	keep    = "Install and keep originals (recommended)"
	discard = "Install and discard originals after success"
	help    = "Help: find owned game files"
	exit    = "Exit"
	back    = "Back to installation"
)

// MaxResponseBytes bounds Pocket Sync answers before the WASM host input is copied.
const MaxResponseBytes = 8192

var choiceSequence uint64

type choice struct {
	Name    string   `json:"name"`
	Query   string   `json:"query"`
	Choices []string `json:"choices"`
}

// Session owns the currently offered menu. Only an answer to that menu can act.
type Session struct {
	root    string
	logger  func(string)
	offered choice
	started bool
	closed  bool
}

func New(root string, logger func(string)) *Session {
	return &Session{root: root, logger: logger}
}

func (s *Session) Start() ([]byte, error) {
	s.started, s.closed = true, false
	return s.menu("")
}

func (s *Session) offer(query string, values ...string) ([]byte, error) {
	s.offered = choice{Name: fmt.Sprintf("vvvvvv-%d", atomic.AddUint64(&choiceSequence, 1)), Query: query, Choices: values}
	return json.Marshal(struct {
		Choice choice `json:"Choice"`
	}{s.offered})
}

func (s *Session) menu(status string) ([]byte, error) {
	return s.offer(status+"Copy your owned data.zip to Assets/vvvvvv/common/source/ on the SD card, then choose Install. Music may already be inside data.zip. If your installation supplies vvvvvvmusic.vvv or mmmmmm.vvv separately, copy those files into the same source folder too. Keeping originals is recommended. Discard removes only that source folder after a successful installation.", keep, discard, help, exit)
}

func (s *Session) guide() ([]byte, error) {
	return s.offer("Find your owned VVVVVV installation. Steam: Library > VVVVVV > Manage > Browse local files. Windows: usually Steam/steamapps/common/VVVVVV. macOS: open VVVVVV.app with Show Package Contents and look in Contents/Resources. Linux: usually ~/.local/share/Steam/steamapps/common/VVVVVV (or your chosen Steam library). For other stores, use their installed game folder. Copy data.zip to Assets/vvvvvv/common/source/ on the SD card. Music may already be inside data.zip, as in Steam's macOS release. If vvvvvvmusic.vvv or optional mmmmmm.vvv is supplied outside the ZIP, copy that file into the same source folder too; a separate file takes precedence over its embedded counterpart. Pocket Sync can access the SD card, so copy these files yourself before installing. No download is needed.", back, exit)
}

func (s *Session) Handle(input []byte) ([]byte, error) {
	if len(input) > MaxResponseBytes {
		return nil, errors.New("plugin response exceeds 8192 bytes")
	}
	if !s.started || s.closed {
		return nil, errors.New("plugin session is not awaiting an answer")
	}
	if bytes.Equal(bytes.TrimSpace(input), []byte(`"Kill"`)) {
		return s.finish()
	}
	object, err := parseObject(input)
	if err != nil {
		return nil, fmt.Errorf("invalid plugin response: %w", err)
	}
	if len(object) != 1 {
		return nil, errors.New("response must contain exactly one Answer or Kill")
	}
	if raw, ok := object["Kill"]; ok {
		if !bytes.Equal(bytes.TrimSpace(raw), []byte("null")) {
			fields, err := parseObject(raw)
			if err != nil || len(fields) != 0 {
				return nil, errors.New("invalid Kill response")
			}
		}
		return s.finish()
	}
	raw, ok := object["Answer"]
	if !ok {
		return nil, errors.New("response must contain Answer or Kill")
	}
	fields, err := parseObject(raw)
	if err != nil || len(fields) != 2 {
		return nil, errors.New("Answer must contain exactly name and value")
	}
	var name, value string
	if err = json.Unmarshal(fields["name"], &name); err != nil || name == "" {
		return nil, errors.New("Answer name must be a nonempty string")
	}
	if err = json.Unmarshal(fields["value"], &value); err != nil || value == "" {
		return nil, errors.New("Answer value must be a nonempty string")
	}
	if name != s.offered.Name {
		return nil, errors.New("answer belongs to an expired choice")
	}
	allowed := false
	for _, offered := range s.offered.Choices {
		if value == offered {
			allowed = true
			break
		}
	}
	if !allowed {
		return nil, errors.New("answer was not offered")
	}
	switch value {
	case exit:
		return s.finish()
	case help:
		return s.guide()
	case back:
		return s.menu("")
	case keep, discard:
		report, err := prep.Install(s.root, value == discard, s.logger)
		if err != nil {
			return s.menu(fmt.Sprintf("Installation failed: %v. Originals were preserved. ", err))
		}
		status := fmt.Sprintf("Installed VVVVVV game data and %d music tracks. ", report.Tracks)
		for _, warning := range report.Warnings {
			status += "Warning: " + warning + " "
		}
		return s.menu(status)
	}
	return nil, errors.New("unsupported choice")
}

func (s *Session) finish() ([]byte, error) {
	s.closed = true
	s.offered = choice{}
	return []byte(`"Exit"`), nil
}

// parseObject rejects duplicate keys, trailing JSON, and non-object values.
func parseObject(input []byte) (map[string]json.RawMessage, error) {
	d := json.NewDecoder(bytes.NewReader(input))
	token, err := d.Token()
	if err != nil || token != json.Delim('{') {
		return nil, errors.New("expected JSON object")
	}
	fields := make(map[string]json.RawMessage)
	for d.More() {
		token, err = d.Token()
		if err != nil {
			return nil, err
		}
		key, ok := token.(string)
		if !ok {
			return nil, errors.New("expected object key")
		}
		if _, duplicate := fields[key]; duplicate {
			return nil, errors.New("duplicate object key")
		}
		var value json.RawMessage
		if err = d.Decode(&value); err != nil {
			return nil, err
		}
		fields[key] = value
	}
	if _, err = d.Token(); err != nil {
		return nil, err
	}
	if _, err = d.Token(); err != io.EOF {
		return nil, errors.New("trailing JSON")
	}
	return fields, nil
}
