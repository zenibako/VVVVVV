// prepare-data validates owned game assets and creates a new Pocket asset folder.
package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"

	"github.com/zenibako/VVVVVV/openfpga/tools/prep"
)

func run(args []string, stdout, stderr io.Writer) int {
	if len(args) != 2 {
		fmt.Fprintln(stderr, "Usage: prepare-data SOURCE OUTPUT")
		return 2
	}
	report, err := prep.Prepare(args[0], args[1], func(message string) { fmt.Fprintln(stderr, message) })
	if err != nil {
		fmt.Fprintln(stderr, err)
		return 1
	}
	if err = json.NewEncoder(stdout).Encode(report); err != nil {
		fmt.Fprintln(stderr, "Cannot write preparation report.")
		return 1
	}
	return 0
}
func main() { os.Exit(run(os.Args[1:], os.Stdout, os.Stderr)) }
