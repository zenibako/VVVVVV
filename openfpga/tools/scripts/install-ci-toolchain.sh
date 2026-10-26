#!/usr/bin/env bash
# Pinned Linux x86_64 build tools; macOS developers may install the same versions.
set -euo pipefail
tool_dir="${1:?usage: install-ci-toolchain.sh OUTPUT_DIRECTORY}"
[[ "$(uname -s)/$(uname -m)" == Linux/x86_64 ]] || { echo 'This installer targets Linux x86_64.' >&2; exit 1; }
mkdir -p "$tool_dir"
curl -fL https://github.com/tinygo-org/tinygo/releases/download/v0.43.0/tinygo0.43.0.linux-amd64.tar.gz -o "$tool_dir/tinygo.tar.gz"
echo "ca9fc6d055e3889462dea4450a2e42a92242d88b5c362a7ec39bdb563413d78a  $tool_dir/tinygo.tar.gz" | sha256sum -c -
tar -xzf "$tool_dir/tinygo.tar.gz" -C "$tool_dir"
curl -fL https://github.com/WebAssembly/binaryen/releases/download/version_133/binaryen-version_133-x86_64-linux.tar.gz -o "$tool_dir/binaryen.tar.gz"
echo "2dc9c7813f5375db93d96ead4b78222fcc3e2677bbb832297af4797782a37489  $tool_dir/binaryen.tar.gz" | sha256sum -c -
tar -xzf "$tool_dir/binaryen.tar.gz" -C "$tool_dir"
if [[ -n "${GITHUB_PATH:-}" ]]; then
    echo "$tool_dir/tinygo/bin" >> "$GITHUB_PATH"
    echo "$tool_dir/binaryen-version_133/bin" >> "$GITHUB_PATH"
fi
