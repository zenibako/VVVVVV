package integration

import (
	"bytes"
	"context"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	extism "github.com/extism/go-sdk"
	"github.com/tetratelabs/wazero"
	"github.com/tetratelabs/wazero/imports/wasi_snapshot_preview1"
	"github.com/zenibako/VVVVVV/openfpga/tools/internal/testgame"
)

type offeredChoice struct {
	Name    string   `json:"name"`
	Query   string   `json:"query"`
	Choices []string `json:"choices"`
}

func offered(t *testing.T, raw []byte) offeredChoice {
	t.Helper()
	var message struct {
		Choice offeredChoice `json:"Choice"`
	}
	if err := json.Unmarshal(raw, &message); err != nil || message.Choice.Name == "" || len(message.Choice.Choices) == 0 {
		t.Fatalf("missing host menu: %s (%v)", raw, err)
	}
	return message.Choice
}

func response(c offeredChoice, index int) []byte {
	raw, _ := json.Marshal(map[string]any{"Answer": map[string]string{"name": c.Name, "value": c.Choices[index]}})
	return raw
}

func build(t *testing.T, output string, args ...string) {
	t.Helper()
	command := exec.Command("go", append([]string{"build", "-trimpath", "-o", output}, args...)...)
	command.Dir = ".."
	if out, err := command.CombinedOutput(); err != nil {
		t.Fatalf("build: %v\n%s", err, out)
	}
}

func nativeOutput(t *testing.T, root string) string {
	t.Helper()
	source := filepath.Join(root, "native-source")
	if err := testgame.WriteSource(source); err != nil {
		t.Fatal(err)
	}
	return nativeForSource(t, root, source)
}

func nativeForSource(t *testing.T, root, source string) string {
	t.Helper()
	cli, output := filepath.Join(root, "prepare-data"), filepath.Join(root, "native-output")
	build(t, cli, "./cmd/prepare-data")
	if data, err := exec.Command(cli, source, output).CombinedOutput(); err != nil {
		t.Fatalf("native CLI: %v\n%s", err, data)
	}
	return output
}

func sameOutputs(t *testing.T, wantDir, gotDir string) {
	t.Helper()
	for _, filename := range []string{"data.zip", "vvvvvv-music.bin"} {
		want, err := os.ReadFile(filepath.Join(wantDir, filename))
		if err != nil {
			t.Fatal(err)
		}
		got, err := os.ReadFile(filepath.Join(gotDir, filename))
		if err != nil {
			t.Fatal(err)
		}
		if !bytes.Equal(want, got) {
			t.Errorf("%s differs: native %d bytes, guest %d bytes", filename, len(want), len(got))
		}
	}
}

func pluginWasm(t *testing.T) string {
	t.Helper()
	wasm := os.Getenv("POCKET_PLUGIN_WASM")
	if wasm == "" {
		compiler := os.Getenv("TINYGO")
		if compiler == "" {
			compiler, _ = exec.LookPath("tinygo")
		}
		if compiler == "" {
			if os.Getenv("REQUIRE_PLUGIN_TESTS") == "1" {
				t.Fatal("TinyGo or POCKET_PLUGIN_WASM is required")
			}
			t.Skip("TinyGo unavailable; set REQUIRE_PLUGIN_TESTS=1 to require actual plugin execution")
		}
		wasm = filepath.Join(t.TempDir(), "plugin.wasm")
		command := exec.Command(compiler, "build", "-p", "2", "-trimpath", "-target", "wasip1", "-buildmode=c-shared", "-o", wasm, "./cmd/pocket-plugin")
		command.Dir = ".."
		if data, err := command.CombinedOutput(); err != nil {
			t.Fatalf("TinyGo build: %v\n%s", err, data)
		}
	}
	return wasm
}

func TestExtismPlugin(t *testing.T) {
	wasm := pluginWasm(t)
	baseline := nativeOutput(t, t.TempDir())
	for _, order := range []string{"card-first", "host-first"} {
		t.Run(order, func(t *testing.T) { exerciseHost(t, wasm, baseline, order) })
	}
}

func exerciseHost(t *testing.T, wasm, baseline, order string) {
	card, host := filepath.Join(t.TempDir(), "card"), t.TempDir()
	common := filepath.Join(card, "Assets/vvvvvv/common")
	source := filepath.Join(common, "source")
	if err := testgame.WriteSource(source); err != nil {
		t.Fatal(err)
	}
	save := filepath.Join(card, "Saves/vvvvvv/common/save.dat")
	if err := os.MkdirAll(filepath.Dir(save), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(save, []byte("existing player progress"), 0644); err != nil {
		t.Fatal(err)
	}
	hostSentinel := filepath.Join(host, "host-sentinel")
	if err := os.WriteFile(hostSentinel, []byte("host untouched"), 0644); err != nil {
		t.Fatal(err)
	}
	fs := wazero.NewFSConfig()
	if order == "card-first" {
		fs = fs.WithDirMount(card, "pocket").WithDirMount(host, "host")
	} else {
		fs = fs.WithDirMount(host, "host").WithDirMount(card, "pocket")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	var logs strings.Builder
	printer := extism.NewHostFunctionWithStack("print_msg", func(_ context.Context, plugin *extism.CurrentPlugin, stack []uint64) {
		message, err := plugin.ReadString(stack[0])
		if err != nil {
			t.Error(err)
		}
		logs.WriteString(message)
		stack[0] = 0
	}, []extism.ValueType{extism.ValueTypeI64}, []extism.ValueType{extism.ValueTypeI64})
	guest, err := extism.NewPlugin(ctx, extism.Manifest{Wasm: []extism.Wasm{extism.WasmFile{Path: wasm}}}, extism.PluginConfig{
		EnableWasi:    true,
		RuntimeConfig: wazero.NewRuntimeConfig().WithCloseOnContextDone(true),
		ModuleConfig:  wazero.NewModuleConfig().WithFSConfig(fs),
	}, []extism.HostFunction{printer})
	if err != nil {
		t.Fatal(err)
	}
	defer guest.Close(ctx)
	call := func(export string, data []byte) []byte {
		t.Helper()
		code, output, err := guest.CallWithContext(ctx, export, data)
		if err != nil || code != 0 {
			t.Fatalf("%s: code %d, %v, %s", export, code, err, output)
		}
		return output
	}
	menu := offered(t, call("start", nil))
	guide := offered(t, call("handle_response", response(menu, 2)))
	for _, platform := range []string{"Windows", "macOS", "Linux"} {
		if !strings.Contains(guide.Query, platform) {
			t.Errorf("guide omits %s", platform)
		}
	}
	for _, bad := range [][]byte{response(menu, 0), []byte(`{"Answer":{"name":"bad","value":"invented"}}`), []byte(`{`), bytes.Repeat([]byte("x"), 8193)} {
		code, _, err := guest.CallWithContext(ctx, "handle_response", bad)
		if code == 0 && err == nil {
			t.Fatalf("actual WASM accepted invalid response %.100s", bad)
		}
	}
	menu = offered(t, call("handle_response", response(guide, 0)))
	menu = offered(t, call("handle_response", response(menu, 0)))
	if !strings.Contains(menu.Query, "Installed") {
		t.Fatalf("keep failed: %s", menu.Query)
	}
	sameOutputs(t, baseline, common)
	if _, err := os.Stat(filepath.Join(source, "data.zip")); err != nil {
		t.Fatalf("keep removed input: %v", err)
	}
	assertNoStaging(t, common)
	menu = offered(t, call("handle_response", response(menu, 1)))
	if !strings.Contains(menu.Query, "Installed") {
		t.Fatalf("discard failed: %s", menu.Query)
	}
	if _, err := os.Stat(source); !os.IsNotExist(err) {
		t.Fatalf("discard left source: %v; plugin status: %s", err, menu.Query)
	}
	sameOutputs(t, baseline, common)
	if logs.Len() == 0 {
		t.Error("plugin emitted no host progress")
	}
	if err := testgame.WriteSource(source); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(source, "vvvvvvmusic.vvv"), []byte("incomplete source"), 0644); err != nil {
		t.Fatal(err)
	}
	menu = offered(t, call("handle_response", response(menu, 1)))
	if !strings.Contains(menu.Query, "failed") {
		t.Fatalf("bad input did not return failure menu: %s", menu.Query)
	}
	if data, err := os.ReadFile(filepath.Join(source, "vvvvvvmusic.vvv")); err != nil || string(data) != "incomplete source" {
		t.Fatalf("failed install changed original: %s %v", data, err)
	}
	sameOutputs(t, baseline, common)
	assertNoStaging(t, common)
	// CRC-valid pages can still form an unbounded, unfinished packet. This used
	// to recurse in the decoder until the actual TinyGo plugin trapped.
	if err := testgame.WriteContinuedPacketSource(source, 5000); err != nil {
		t.Fatal(err)
	}
	unchanged := make(map[string][]byte)
	for _, name := range []string{"data.zip", "vvvvvvmusic.vvv"} {
		data, err := os.ReadFile(filepath.Join(source, name))
		if err != nil {
			t.Fatal(err)
		}
		unchanged[name] = data
	}
	menu = offered(t, call("handle_response", response(menu, 1)))
	if !strings.Contains(menu.Query, "failed") {
		t.Fatalf("unfinished Ogg packet did not return failure menu: %s", menu.Query)
	}
	for name, original := range unchanged {
		data, err := os.ReadFile(filepath.Join(source, name))
		if err != nil || !bytes.Equal(data, original) {
			t.Fatalf("unfinished Ogg failure changed %s: %v", name, err)
		}
	}
	sameOutputs(t, baseline, common)
	assertNoStaging(t, common)
	if err := testgame.WriteSource(source); err != nil {
		t.Fatal(err)
	}
	menu = offered(t, call("handle_response", response(menu, 0)))
	if !strings.Contains(menu.Query, "Installed") {
		t.Fatalf("retry failed: %s", menu.Query)
	}
	if err := testgame.WriteEmbeddedMusicSource(source, false); err != nil {
		t.Fatal(err)
	}
	embeddedNative := nativeForSource(t, t.TempDir(), source)
	menu = offered(t, call("handle_response", response(menu, 0)))
	if !strings.Contains(menu.Query, "Installed") {
		t.Fatalf("embedded-only install failed: %s", menu.Query)
	}
	sameOutputs(t, embeddedNative, common)
	entries, err := os.ReadDir(source)
	if err != nil || len(entries) != 1 || entries[0].Name() != "data.zip" {
		t.Fatalf("embedded source changed: %v %v", entries, err)
	}
	assertNoStaging(t, common)
	if out := call("handle_response", []byte(`{"Kill":{}}`)); string(out) != `"Exit"` {
		t.Fatalf("Kill: %s", out)
	}
	if _, err := os.Stat(filepath.Join(source, "data.zip")); err != nil {
		t.Fatalf("cancel removed input: %v", err)
	}
	if data, err := os.ReadFile(save); err != nil || string(data) != "existing player progress" {
		t.Fatalf("save changed: %s %v", data, err)
	}
	if data, err := os.ReadFile(hostSentinel); err != nil || string(data) != "host untouched" {
		t.Fatalf("host changed: %s %v", data, err)
	}
}

func assertNoStaging(t *testing.T, common string) {
	t.Helper()
	entries, err := os.ReadDir(common)
	if err != nil {
		t.Fatal(err)
	}
	for _, entry := range entries {
		if strings.HasPrefix(entry.Name(), ".vvvvvv-stage-") {
			t.Errorf("installation left hidden staging folder %s", entry.Name())
		}
	}
}

func TestStandardGoWASICommand(t *testing.T) {
	root := t.TempDir()
	baseline := nativeOutput(t, root)
	source := filepath.Join(root, "wasi-source")
	if err := testgame.WriteSource(source); err != nil {
		t.Fatal(err)
	}
	wasm := filepath.Join(root, "prepare-data.wasm")
	command := exec.Command("go", "build", "-trimpath", "-o", wasm, "./cmd/prepare-data")
	command.Dir = ".."
	command.Env = append(os.Environ(), "GOOS=wasip1", "GOARCH=wasm")
	if data, err := command.CombinedOutput(); err != nil {
		t.Fatalf("build WASI CLI: %v\n%s", err, data)
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Minute)
	defer cancel()
	runtime := wazero.NewRuntimeWithConfig(ctx, wazero.NewRuntimeConfig().WithCloseOnContextDone(true))
	defer runtime.Close(ctx)
	wasi_snapshot_preview1.MustInstantiate(ctx, runtime)
	module, err := os.ReadFile(wasm)
	if err != nil {
		t.Fatal(err)
	}
	var stdout, stderr bytes.Buffer
	_, err = runtime.InstantiateWithConfig(ctx, module, wazero.NewModuleConfig().WithFSConfig(wazero.NewFSConfig().WithDirMount(root, "/work")).WithArgs("prepare-data", "/work/wasi-source", "/work/wasi-output").WithStdout(&stdout).WithStderr(&stderr))
	if err != nil {
		t.Fatalf("execute WASI CLI: %v\n%s\n%s", err, stdout.String(), stderr.String())
	}
	sameOutputs(t, baseline, filepath.Join(root, "wasi-output"))
}
