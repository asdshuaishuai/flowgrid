package main

import (
	"sync"
	"testing"

	"github.com/flowgrid/protocol"
)

// FFI exports are called from multiple OS threads (Qt + tokio); the handle
// registries must survive concurrent put/get/remove without data races.
// Run with -race to verify.
func TestRegistryConcurrentAccess(t *testing.T) {
	var reg registry[*protocol.LatencyMonitor]

	const workers = 8
	const iters = 200

	var wg sync.WaitGroup
	for w := 0; w < workers; w++ {
		wg.Add(1)
		go func(w int) {
			defer wg.Done()
			for i := 0; i < iters; i++ {
				id := reg.put(protocol.NewLatencyMonitor(4))
				if _, ok := reg.get(id); !ok {
					t.Errorf("handle %d missing right after put", id)
					return
				}
				reg.remove(id)
				if _, ok := reg.get(id); ok {
					t.Errorf("handle %d still present after remove", id)
					return
				}
			}
		}(w)
	}
	wg.Wait()

	reg.mu.Lock()
	n := len(reg.items)
	reg.mu.Unlock()
	if n != 0 {
		t.Errorf("registry leaked %d entries", n)
	}
}

func TestKeymapFFILookupMatchesGo(t *testing.T) {
	kmf, err := protocol.ParseKeyMap([]byte("rules:\n- fromOS: macos\n  toOS: windows\n  fromKey: 6\n  toKey: 7\n  context: global\n"))
	if err != nil {
		t.Fatalf("ParseKeyMap: %v", err)
	}
	table := protocol.NewKeyMapTable(kmf)

	key, mods, found := table.Lookup(protocol.PlatformMacOS, protocol.PlatformWindows, 6, 0, protocol.ContextGlobal)
	if !found || key != 7 || mods != 0 {
		t.Errorf("lookup: got key=%d mods=%d found=%v, want key=7 found=true", key, mods, found)
	}
}
