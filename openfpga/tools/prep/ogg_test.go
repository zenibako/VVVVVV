package prep

import (
	"path/filepath"
	"strings"
	"testing"

	"github.com/zenibako/VVVVVV/openfpga/tools/internal/testgame"
)

func TestOggPreflightBoundsBeforeDecoder(t *testing.T) {
	for _, tc := range []struct {
		name   string
		data   []byte
		reason string
	}{
		{"recursive-continuation", testgame.ContinuedPacketOgg(5000), "too many pages"},
		{"oversized-packet", testgame.OversizedPacketOgg(), "packet exceeds"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if e := validateOggPages(tc.data); e == nil || !strings.Contains(e.Error(), tc.reason) {
				t.Fatalf("preflight: %v", e)
			}
			if e := validateOgg(tc.data); e == nil {
				t.Fatal("decoder reached malformed stream")
			}
		})
	}
}

func TestContinuedPacketSourceRejectedWithoutPublishing(t *testing.T) {
	s := filepath.Join(t.TempDir(), "source")
	if e := testgame.WriteContinuedPacketSource(s, 5000); e != nil {
		t.Fatal(e)
	}
	if _, e := Prepare(s, filepath.Join(t.TempDir(), "out"), nil); e == nil {
		t.Fatal("accepted continuation bomb")
	}
}
