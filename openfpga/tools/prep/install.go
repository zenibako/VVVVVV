package prep

import (
	"os"
	"path/filepath"
)

type fileOps struct {
	rename    func(string, string) error
	removeAll func(string) error
}

// Install prepares the fixed card source folder and replaces only the game's
// two runtime assets. Saves and all other files in common are left alone.
func Install(cardRoot string, discard bool, progress func(string)) (Report, error) {
	return install(cardRoot, discard, progress, fileOps{rename: os.Rename})
}

func install(cardRoot string, discard bool, progress func(string), ops fileOps) (Report, error) {
	if ops.removeAll == nil {
		ops.removeAll = removeTree
	}
	var report Report
	common := filepath.Join(cardRoot, "Assets", "vvvvvv", "common")
	source := filepath.Join(common, "source")
	if e := safePath(common, true); e != nil {
		return report, e
	}
	// Check both destinations before creating anything or decoding music.
	existed := map[string]bool{}
	for _, name := range outputNames {
		p := filepath.Join(common, name)
		if _, e := os.Lstat(p); os.IsNotExist(e) {
			continue
		} else if e != nil {
			return report, problem("Cannot inspect existing output.")
		}
		if e := safePath(p, false); e != nil {
			return report, e
		}
		existed[name] = true
	}
	stage, e := os.MkdirTemp(common, ".vvvvvv-stage-")
	if e != nil {
		return report, problem("Cannot create card staging folder.")
	}
	// A backup is retained if rollback fails; deleting it would destroy recovery data.
	cleanup := true
	defer func() {
		if cleanup {
			removeTree(stage)
		}
	}()
	report, e = prepareInto(source, stage, progress)
	if e != nil {
		return report, e
	}
	backup := filepath.Join(stage, "backup")
	if e = os.Mkdir(backup, 0700); e != nil {
		return report, problem("Cannot create card backup folder.")
	}
	moved := []string{}
	published := []string{}
	oldHashes := map[string]string{}
	rollback := func() error {
		good := true
		for i := len(published) - 1; i >= 0; i-- {
			if e := os.Remove(filepath.Join(common, published[i])); e != nil && !os.IsNotExist(e) {
				good = false
			}
		}
		for i := len(moved) - 1; i >= 0; i-- {
			name := moved[i]
			if e := ops.rename(filepath.Join(backup, name), filepath.Join(common, name)); e != nil {
				good = false
			}
		}
		if !good {
			cleanup = false
			return problem("Installation failed and rollback could not finish. Recovery files remain in the hidden card staging folder; originals were kept.")
		}
		for _, name := range moved {
			h, e := hashFile(filepath.Join(common, name))
			if e != nil || h != oldHashes[name] {
				cleanup = false
				return problem("Rollback verification failed. Recovery files remain in the hidden card staging folder; originals were kept.")
			}
		}
		return problem("Installation failed; previous outputs were restored and originals were kept.")
	}
	// Capture old hashes before moving, so rollback is checked rather than assumed.
	for _, name := range outputNames {
		if existed[name] {
			h, e := hashFile(filepath.Join(common, name))
			if e != nil {
				return report, e
			}
			oldHashes[name] = h
		}
	}
	for _, name := range outputNames {
		if existed[name] {
			if e = ops.rename(filepath.Join(common, name), filepath.Join(backup, name)); e != nil {
				return report, rollback()
			}
			moved = append(moved, name)
		}
	}
	for _, name := range outputNames {
		if e = ops.rename(filepath.Join(stage, name), filepath.Join(common, name)); e != nil {
			return report, rollback()
		}
		published = append(published, name)
	}
	if e = verify(common, report); e != nil {
		return report, rollback()
	}
	if discard {
		// Only the dedicated source folder can be discarded, after verification.
		if e = safePath(source, true); e != nil {
			report.Warnings = append(report.Warnings, "Prepared successfully; source folder could not be safely discarded.")
		} else if e = ops.removeAll(source); e != nil {
			report.Warnings = append(report.Warnings, "Prepared successfully; some source files could not be removed.")
		}
	}
	if e = ops.removeAll(stage); e != nil {
		report.Warnings = append(report.Warnings, "Prepared successfully; hidden staging cleanup could not finish.")
	}
	return report, nil
}
