// Tests for the emulated DataStore: a full store makes room only by dropping stale entries,
// as the firmware's DataStore::put does.
package sim

import (
	"fmt"
	"testing"
	"time"
)

func TestStoreFullEvictsStale(t *testing.T) {
	s := NewStore()
	s.Put("old.a", 1.0, "host", time.Nanosecond) // stale at once
	s.Put("old.b", 1.0, "host", time.Nanosecond)
	for i := len(s.m); i < maxEntries; i++ {
		if !s.Put(fmt.Sprintf("k%d", i), 1.0, "push", 0) {
			t.Fatalf("put k%d refused before the store was full", i)
		}
	}
	time.Sleep(time.Millisecond)
	if !s.Put("new.a", 2.0, "push", 0) || !s.Put("new.b", 2.0, "push", 0) {
		t.Fatal("a full store with stale entries must take new keys")
	}
	if _, ok := s.Get("old.a"); ok {
		t.Error("old.a should have been dropped")
	}
	if s.Put("new.c", 2.0, "push", 0) {
		t.Error("a full store without stale entries must drop new keys")
	}
	if len(s.m) != maxEntries {
		t.Errorf("size %d, want %d", len(s.m), maxEntries)
	}
}
