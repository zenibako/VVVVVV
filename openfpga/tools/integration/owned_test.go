package integration

import (
	"context"
	"crypto/sha256"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	extism "github.com/extism/go-sdk"
	"github.com/tetratelabs/wazero"
)

// TestOwnedGamePlugin is opt-in: all copied assets and converted outputs live in
// testing-managed temporary directories and are removed when the test finishes.
func TestOwnedGamePlugin(t *testing.T) {
	input := os.Getenv("VVVVVV_TEST_SOURCE")
	if input == "" {
		t.Skip("set VVVVVV_TEST_SOURCE to an owned game's installed asset directory")
	}
	wasm := pluginWasm(t)
	root := t.TempDir()
	nativeSource := filepath.Join(root, "source")
	inputHashes := copyKnownAssets(t, input, nativeSource)
	native := nativeForSource(t, root, nativeSource)
	for _, order := range []string{"card-first", "host-first"} {
		t.Run(order, func(t *testing.T) {
			card, host := t.TempDir(), t.TempDir()
			common := filepath.Join(card, "Assets/vvvvvv/common")
			source := filepath.Join(common, "source")
			copyKnownAssets(t, input, source)
			save := filepath.Join(card, "Saves/vvvvvv/common/save.dat")
			if err := os.MkdirAll(filepath.Dir(save), 0700); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(save, []byte("owned game test progress"), 0600); err != nil {
				t.Fatal(err)
			}
			fs := wazero.NewFSConfig()
			if order == "card-first" {
				fs = fs.WithDirMount(card, "pocket").WithDirMount(host, "host")
			} else {
				fs = fs.WithDirMount(host, "host").WithDirMount(card, "pocket")
			}
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Minute)
			defer cancel()
			printer := extism.NewHostFunctionWithStack("print_msg", func(_ context.Context, _ *extism.CurrentPlugin, stack []uint64) { stack[0] = 0 }, []extism.ValueType{extism.ValueTypeI64}, []extism.ValueType{extism.ValueTypeI64})
			guest, err := extism.NewPlugin(ctx, extism.Manifest{Wasm: []extism.Wasm{extism.WasmFile{Path: wasm}}}, extism.PluginConfig{
				EnableWasi:    true,
				RuntimeConfig: wazero.NewRuntimeConfig().WithCloseOnContextDone(true),
				ModuleConfig:  wazero.NewModuleConfig().WithFSConfig(fs),
			}, []extism.HostFunction{printer})
			if err != nil {
				t.Fatal(err)
			}
			defer guest.Close(ctx)
			code, output, err := guest.CallWithContext(ctx, "start", nil)
			if err != nil || code != 0 {
				t.Fatalf("start: code %d, %v", code, err)
			}
			menu := offered(t, output)
			code, output, err = guest.CallWithContext(ctx, "handle_response", response(menu, 0))
			if err != nil || code != 0 {
				t.Fatalf("owned install: code %d, %v", code, err)
			}
			menu = offered(t, output)
			if !strings.Contains(menu.Query, "Installed") {
				t.Fatalf("owned install failed: %s", menu.Query)
			}
			for _, name := range []string{"data.zip", "vvvvvv-music.bin"} {
				want, got := fileDigest(t, filepath.Join(native, name)), fileDigest(t, filepath.Join(common, name))
				if want != got {
					t.Errorf("owned %s differs from native output", name)
				}
				t.Logf("%s SHA256 %x", name, got)
			}
			assertSourceHashes(t, source, inputHashes)
			assertNoStaging(t, common)
			if got, err := os.ReadFile(save); err != nil || string(got) != "owned game test progress" {
				t.Fatalf("owned save changed: %v", err)
			}
		})
	}
	assertSourceHashes(t, input, inputHashes)
}

func fileDigest(t *testing.T, path string) [32]byte {
	t.Helper()
	file, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()
	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		t.Fatal(err)
	}
	var digest [32]byte
	copy(digest[:], hash.Sum(nil))
	return digest
}

// Only these known game assets may leave the supplied installation directory.
func copyKnownAssets(t *testing.T, input, output string) map[string][32]byte {
	t.Helper()
	if err := os.MkdirAll(output, 0700); err != nil {
		t.Fatal(err)
	}
	hashes := make(map[string][32]byte)
	for _, name := range []string{"data.zip", "vvvvvvmusic.vvv", "mmmmmm.vvv"} {
		path := filepath.Join(input, name)
		info, err := os.Lstat(path)
		if os.IsNotExist(err) && name != "data.zip" {
			continue
		}
		if err != nil {
			t.Fatal(err)
		}
		if !info.Mode().IsRegular() {
			t.Fatalf("%s must be a regular file", name)
		}
		file, err := os.Open(path)
		if err != nil {
			t.Fatal(err)
		}
		copyFile, err := os.OpenFile(filepath.Join(output, name), os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
		if err != nil {
			file.Close()
			t.Fatal(err)
		}
		_, copyErr := io.Copy(copyFile, file)
		file.Close()
		closeErr := copyFile.Close()
		if copyErr != nil {
			t.Fatal(copyErr)
		}
		if closeErr != nil {
			t.Fatal(closeErr)
		}
		hashes[name] = fileDigest(t, path)
	}
	return hashes
}

func assertSourceHashes(t *testing.T, source string, hashes map[string][32]byte) {
	t.Helper()
	for name, want := range hashes {
		if fileDigest(t, filepath.Join(source, name)) != want {
			t.Errorf("owned original %s changed", name)
		}
	}
}
