package prep

import (
	"os"
	"path/filepath"
)

// TinyGo 0.43's os.RemoveAll returns ENOSYS on WASI. Use the portable
// Lstat/ReadDir/Remove primitives; links are removed, never traversed. Cleanup
// has finite work and stack bounds even if a source folder contains extras.
func removeTree(root string) error {
	clean := filepath.Clean(root)
	if root == "" || clean == "." || clean == filepath.VolumeName(clean)+string(filepath.Separator) {
		return problem("Refusing unsafe cleanup root.")
	}
	remaining := 16384
	var remove func(string, int) error
	remove = func(p string, depth int) error {
		remaining--
		if depth > 64 || remaining < 0 {
			return problem("Cleanup folder exceeds safety limits.")
		}
		info, e := os.Lstat(p)
		if os.IsNotExist(e) {
			return nil
		}
		if e != nil {
			return e
		}
		if !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
			return os.Remove(p)
		}
		entries, e := os.ReadDir(p)
		if e != nil {
			return e
		}
		if len(entries) > remaining {
			return problem("Cleanup folder exceeds safety limits.")
		}
		for _, entry := range entries {
			if e = remove(filepath.Join(p, entry.Name()), depth+1); e != nil {
				return e
			}
		}
		return os.Remove(p)
	}
	return remove(clean, 0)
}
