package sim

import (
	"encoding/json"
	"fmt"
	"slices"
	"strings"
)

// Pages returns ui.pages: the built-in pages to show, in swipe order. Missing = all.
func (n *NVS) Pages() ([]string, error) {
	raw, ok := n.Get("ui.pages")
	if !ok {
		return append([]string(nil), builtinPages...), nil
	}
	b, _ := json.Marshal(raw)
	var pages []string
	if err := json.Unmarshal(b, &pages); err != nil {
		return nil, fmt.Errorf("ui.pages: must be a list of page names")
	}
	if len(pages) == 0 {
		return nil, fmt.Errorf("ui.pages: at least one page is needed")
	}
	seen := map[string]bool{}
	for _, p := range pages {
		if !slices.Contains(builtinPages, p) {
			return nil, fmt.Errorf("ui.pages: unknown page %q (use %s)", p, strings.Join(builtinPages, ", "))
		}
		if seen[p] {
			return nil, fmt.Errorf("ui.pages: %q is listed twice", p)
		}
		seen[p] = true
	}
	return pages, nil
}

// homePageLocked is where "swipe up" goes: the eyes, or the first page if they are hidden.
func (d *Device) homePageLocked() string {
	pages, err := d.nvs.Pages()
	if err != nil || slices.Contains(pages, "eyes") {
		return "eyes"
	}
	return pages[0]
}
