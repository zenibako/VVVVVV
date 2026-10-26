GO ?= go
TINYGO ?= tinygo
BUILD := build

.PHONY: all test cli native wasi plugin clean
all: native wasi plugin

test:
	$(GO) test ./...
	$(GO) vet ./...
	$(GO) test -C third_party/pocket-music ./...
	$(GO) test -C third_party/pocket-music/third_party/vorbis ./...
	$(GO) test -C third_party/pocket-music/third_party/oggvorbis ./...

cli:
	@mkdir -p $(BUILD)
	$(GO) build -trimpath -o $(BUILD)/prepare-data ./cmd/prepare-data

native:
	@mkdir -p $(BUILD)
	@set -e; for target in darwin/amd64 darwin/arm64 linux/amd64 linux/arm64 windows/amd64 windows/arm64; do \
		os=$${target%/*}; arch=$${target#*/}; ext=; \
		if [ "$$os" = windows ]; then ext=.exe; fi; \
		CGO_ENABLED=0 GOOS=$$os GOARCH=$$arch $(GO) build -trimpath \
			-o $(BUILD)/prepare-data-$$os-$$arch$$ext ./cmd/prepare-data; \
	done

wasi:
	@mkdir -p $(BUILD)
	GOOS=wasip1 GOARCH=wasm CGO_ENABLED=0 $(GO) build -trimpath \
		-o $(BUILD)/prepare-data.wasm ./cmd/prepare-data

plugin:
	@mkdir -p $(BUILD)
	$(TINYGO) build -p 2 -trimpath -target wasip1 -buildmode=c-shared \
		-o $(BUILD)/plugin.wasm ./cmd/pocket-plugin
	cp plugin.json $(BUILD)/plugin.json
	cp plugin_icon.png $(BUILD)/plugin_icon.png
