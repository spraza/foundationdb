package main

import (
	"fmt"
	"os"
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}

func run() error {
	// Check command-line arguments
	if len(os.Args) != 2 {
		return fmt.Errorf("usage: %s <trace-file.xml>", os.Args[0])
	}

	traceFile := os.Args[1]

	// Parse the trace file
	fmt.Fprintf(os.Stderr, "Loading trace file: %s\n", traceFile)
	traceData, err := parseTraceFile(traceFile)
	if err != nil {
		return fmt.Errorf("failed to parse trace file: %w", err)
	}

	fmt.Fprintf(os.Stderr, "Loaded %d events (%.2fs - %.2fs)\n",
		len(traceData.Events), traceData.MinTime, traceData.MaxTime)
	fmt.Fprintf(os.Stderr, "Found %d DB configurations\n", len(traceData.Configs))
	fmt.Fprintf(os.Stderr, "Found %d recovery states\n", len(traceData.RecoveryStates))

	// Start the TUI
	return runUI(traceData)
}
