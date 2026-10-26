//go:build wasip1

package main

import (
	"errors"

	"github.com/extism/go-pdk"
	"github.com/zenibako/VVVVVV/openfpga/tools/plugin"
)

//go:wasmimport extism:host/user print_msg
func printMsg(uint64) uint64

//go:wasmimport extism:host/env input_length
func inputLength() uint64

var session = plugin.New("/pocket", func(message string) {
	allocation := pdk.AllocateString(message + "\n")
	defer allocation.Free()
	printMsg(allocation.Offset())
})

func output(data []byte, err error) int32 {
	if err != nil {
		pdk.SetError(err)
		return 1
	}
	pdk.Output(data)
	return 0
}

//go:wasmexport start
func start() int32 { return output(session.Start()) }

//go:wasmexport handle_response
func handleResponse() int32 {
	if inputLength() > plugin.MaxResponseBytes {
		return output(nil, errors.New("plugin response exceeds 8192 bytes"))
	}
	return output(session.Handle(pdk.Input()))
}

func main() {}
