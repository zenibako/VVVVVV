#!/usr/bin/env bash
# Run a command in the openfpgaOS SDK toolchain container with the whole
# VVVVVV checkout mounted at the same path, so the device build can read the
# game sources in ../src and ../../third_party.  (The SDK's own
# sdk-container.sh mounts only the SDK.)
set -euo pipefail

POCKET_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$POCKET_ROOT/../.." && pwd -P)"
SDK_ROOT="${SDK_ROOT:-$POCKET_ROOT/.deps/openfpgaos-sdk}"
IMG="${SDK_IMG:-openfpgaos-firmware}"
DOCKERFILE="${SDK_DOCKERFILE:-$SDK_ROOT/tools/docker/Dockerfile.firmware}"

source "$SDK_ROOT/tools/oci.sh"

WORKDIR="$(pwd -P)"
if [[ "${1:-}" == "-C" ]]; then
    WORKDIR="$(cd "$2" && pwd -P)"
    shift 2
fi
[[ $# -ge 1 ]] || { echo "usage: sdk-container.sh [-C dir] command [args...]" >&2; exit 2; }

if ! oci_image_exists "$IMG"; then
    [[ -f "$DOCKERFILE" ]] || { echo "Missing $DOCKERFILE" >&2; exit 1; }
    echo "[sdk] building $IMG (one-time)"
    oci_build -t "$IMG" -f "$DOCKERFILE" "$(dirname "$DOCKERFILE")"
fi

tty_args=()
[[ -t 0 && -t 1 ]] && tty_args=(-i -t)
stdin_source=/dev/null
[[ ${#tty_args[@]} -gt 0 ]] && stdin_source=/dev/tty

container_name="vvvvvv-sdk-$$"
trap 'oci_rm_force "$container_name"' EXIT INT TERM

# The container runs in the background and the script waits for it, so an
# INT or TERM reaches the trap at once and the container is removed.
#
# CPATH is cleared because the image points it at picolibc for firmware
# builds, and apps link musl.  OF_SDK_IN_CONTAINER=1 tells sdk.mk it is
# inside the toolchain so it compiles instead of re-entering this script.
oci_run --rm --name "$container_name" ${tty_args[@]+"${tty_args[@]}"} \
    --user "$(id -u):$(id -g)" \
    -v "$REPO_ROOT:$REPO_ROOT" \
    --tmpfs /sdkhome:exec \
    -e HOME=/sdkhome \
    -e CPATH= \
    -e OF_SDK_IN_CONTAINER=1 \
    -e "MAKEFLAGS=${MAKEFLAGS:-}" \
    -w "$WORKDIR" \
    "$IMG" "$@" < "$stdin_source" &
wait $!
