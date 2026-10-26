// Command pocket-music converts a game's music into a pack for its
// openfpgaOS port.
//
//	pocket-music vvvvvv [-mmmmmm mmmmmm.vvv] vvvvvvmusic.vvv vvvvvv-music.bin
//
// The output is written next to its final name and renamed into place only
// once complete; an existing output is left alone.
package main

import (
	"bufio"
	"errors"
	"flag"
	"fmt"
	"os"
	"path/filepath"

	"github.com/zenibako/pocket-music/pack"
	"github.com/zenibako/pocket-music/vvvvvv"
)

func usage() {
	fmt.Fprintln(os.Stderr, "usage: pocket-music vvvvvv [-mmmmmm mmmmmm.vvv] vvvvvvmusic.vvv vvvvvv-music.bin")
	os.Exit(2)
}

func main() {
	if len(os.Args) < 2 || os.Args[1] != "vvvvvv" {
		usage()
	}
	flags := flag.NewFlagSet("vvvvvv", flag.ExitOnError)
	mmmmmmPath := flags.String("mmmmmm", "", "MMMMMM's mmmmmm.vvv, to include its soundtrack")
	flags.Usage = usage
	flags.Parse(os.Args[2:])
	if flags.NArg() != 2 {
		usage()
	}
	if err := run(flags.Arg(0), *mmmmmmPath, flags.Arg(1)); err != nil {
		fmt.Fprintln(os.Stderr, "pocket-music:", err)
		os.Exit(1)
	}
}

func run(ppppppPath, mmmmmmPath, outPath string) error {
	if _, err := os.Stat(outPath); err == nil {
		return fmt.Errorf("%s already exists", outPath)
	}
	pppppp, err := os.ReadFile(ppppppPath)
	if err != nil {
		return err
	}
	var mmmmmm []byte
	if mmmmmmPath != "" {
		if mmmmmm, err = os.ReadFile(mmmmmmPath); err != nil {
			return err
		}
	}
	tracks, err := vvvvvv.Build(pppppp, mmmmmm,
		func(name string) { fmt.Println("Converting", name) },
		func(warning string) { fmt.Fprintln(os.Stderr, "warning:", warning) })
	if err != nil {
		return err
	}
	tmp, err := os.CreateTemp(filepath.Dir(outPath), ".pocket-music-*")
	if err != nil {
		return err
	}
	w := bufio.NewWriter(tmp)
	err = pack.Write(w, tracks)
	err = errors.Join(err, w.Flush(), tmp.Chmod(0o644), tmp.Close())
	if err == nil {
		err = os.Rename(tmp.Name(), outPath)
	}
	if err != nil {
		os.Remove(tmp.Name())
		return err
	}
	fmt.Printf("Wrote %s: %d tracks\n", outPath, len(tracks))
	return nil
}
