#!/usr/bin/env python3
"""Host contract check using the locked Pocket runtime's actual slot lookup.

This compiles selected kernel/HAL code and this port's SDL_Init; it does not
exercise APF loading, hardware audio or physical save persistence.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

PORT = Path(__file__).resolve().parents[1]
KERNEL = "src/firmware/os/kernel/syscall.c"
POCKET_HAL = "src/firmware/os/targets/pocket/file.c"


def git(runtime, *args):
    return subprocess.run(["git", "-C", str(runtime), *args], check=True,
                          capture_output=True, text=True, timeout=30).stdout


def between(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def check(runtime, system_source=None):
    pin = json.loads((PORT / "release/runtime.lock.json").read_text())["runtime"]["commit"]
    if git(runtime, "rev-parse", "HEAD").strip() != pin:
        raise ValueError("Runtime HEAD differs from runtime.lock.json")
    # Read Git objects so uncommitted runtime edits cannot impersonate the pin.
    kernel = git(runtime, "show", pin + ":" + KERNEL)
    pocket = git(runtime, "show", pin + ":" + POCKET_HAL)
    if system_source is None:
        system_source = (PORT / "sdl3/system.c").read_text()

    limits = "\n".join(re.search(r"^#define " + name + r"\s+\d+", kernel, re.M).group()
                       for name in ("MAX_FILE_SLOTS", "FILE_SLOT_NAME_MAX"))
    entry = re.search(r"typedef struct \{\s+uint32_t\s+slot_id;\s+"
                      r"char\s+filename\[FILE_SLOT_NAME_MAX\];\s+"
                      r"\} file_slot_entry_t;", kernel).group()
    lookup = between(kernel, "void file_slot_register(", "static int save_slot_from_data_id(")
    basename_copy = between(pocket, "    /* Extract basename (after last '/') */",
                            "\n}\n\nint of_file_slot_write(")
    resolve_name = re.search(r"^int of_file_resolve_name\([^\n]+", pocket, re.M).group()
    sdl_init = between(system_source, "bool SDL_Init(", "\nvoid SDL_Quit(")

    harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OF_PLATFORM_POCKET 1
#define OF_TARGET_PLATFORM_ID OF_PLATFORM_POCKET
#define of_term_printf(...) fprintf(stderr, __VA_ARGS__)
typedef unsigned int SDL_InitFlags;
'''
    harness += limits + "\n" + entry + r'''
static file_slot_entry_t file_slots[MAX_FILE_SLOTS];
static int file_slot_count;

/* The Pocket branch must never ask for a live GETFILE during lookup. */
int of_file_get_name(uint32_t slot, char *out, uint32_t size) {
    (void)slot; (void)out; (void)size;
    abort();
}
'''
    harness += resolve_name + "\n" + lookup
    harness += "\nstatic int pocket_basename(const char *filename, char *name_out, uint32_t name_max) {\n"
    harness += basename_copy + "\n}\n"
    harness += "\n#define of_file_slot_register file_slot_register\n" + sdl_init
    harness += r'''
static void selected_slot(uint32_t slot, const char *filename) {
    char name[FILE_SLOT_NAME_MAX];
    assert(strlen(filename) < FILE_SLOT_NAME_MAX);
    assert(pocket_basename(filename, name, sizeof(name)) == 0);
    file_slot_register(slot, name);
}

static void expect_slot(const char *name, int expected) {
    int actual = file_slot_lookup(name);
    if (actual != expected) {
        fprintf(stderr, "%s: expected slot %d, got %d\n", name, expected, actual);
        exit(1);
    }
}

int main(void) {
    assert(FILE_SLOT_NAME_MAX == 24);
#ifdef MAKEANDPLAY
    selected_slot(4, "mp/vvvvvv-mp-data.zip");
    selected_slot(6, "mp/vvvvvv-mp-music.bin");
    selected_slot(10, "vvvvvv-mp_0.sav");
    expect_slot("mp/vvvvvv-mp-data.zip", 4);
    expect_slot("vvvvvv-mp-music.bin", 6);
    expect_slot("vvvvvv-music.bin", -1);
    expect_slot("vvvvvv-mp_0.sav", 10);
    expect_slot("vvvvvv_0.sav", -1);
    assert(SDL_Init(0));
    expect_slot("mp/vvvvvv-mp-data.zip", 4);
    expect_slot("vvvvvv-music.bin", 6);
    expect_slot("vvvvvv-mp-music.bin", 6);
    expect_slot("vvvvvv-mp_0.sav", 10);
    expect_slot("vvvvvv_0.sav", -1);
    assert(file_slot_count == 4);
    puts("PASS: Make and Play selected-slot names and SDL music alias");
#else
    selected_slot(4, "data.zip");
    selected_slot(6, "vvvvvv-music.bin");
    selected_slot(10, "vvvvvv_0.sav");
    int before = file_slot_count;
    assert(SDL_Init(0));
    assert(file_slot_count == before);
    expect_slot("data.zip", 4);
    expect_slot("vvvvvv-music.bin", 6);
    expect_slot("vvvvvv_0.sav", 10);
    expect_slot("vvvvvv-mp-data.zip", -1);
    expect_slot("vvvvvv-mp-music.bin", -1);
    expect_slot("vvvvvv-mp_0.sav", -1);
    puts("PASS: standard edition leaves selected-slot names unchanged");
#endif
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="vvvvvv-slot-contract-") as directory:
        temp = Path(directory)
        source = temp / "slots.c"
        source.write_text(harness)
        for edition in (False, True):
            executable = temp / ("makeandplay" if edition else "standard")
            flags = ["-DMAKEANDPLAY"] if edition else []
            subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", *flags,
                            str(source), "-o", str(executable)], check=True, timeout=60)
            subprocess.run([str(executable)], check=True, timeout=10)
    print("Runtime pin:", pin, "(host slot contract only)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-source", type=Path, required=True)
    args = parser.parse_args()
    check(args.runtime_source)
