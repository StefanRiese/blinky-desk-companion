package firmware

import "testing"

func TestNewer(t *testing.T) {
	for _, c := range []struct {
		a, b string
		want bool
	}{
		{"0.2.0", "0.1.3", true},
		{"0.2.0", "0.2.0-rc1", true},
		{"0.2.0-rc1", "0.2.0", false},
		{"0.2.1-dev.27+802b7bc", "0.2.1-dev.9+aaaaaaa", true},
		{"0.2.1-dev.9", "0.2.1-dev.27", false},
		{"0.2.1-dev.27", "0.2.0", true},
		{"0.2.0", "0.2.1-dev.27", false},
		{"v1.0.0", "0.9.9", true},
		{"0.2.0", "0.2.0", false},
		{"0.2.0", "7aff8b1", true}, // a bare commit is older than any version
		{"7aff8b1", "0.2.0", false},
	} {
		if got := Newer(c.a, c.b); got != c.want {
			t.Errorf("Newer(%q, %q) = %v, want %v", c.a, c.b, got, c.want)
		}
	}
}

func TestOffersFor(t *testing.T) {
	app := []Asset{{Name: "blinky-app-amoled175.bin"}}
	rels := []Release{
		{Tag: MainTag, Name: "Latest build from main (0.3.1-dev.4+abc1234)", Prerelease: true, Assets: app},
		{Tag: "v0.4.0-rc1", Prerelease: true, Assets: app},
		{Tag: "v0.3.0", Assets: app},
		{Tag: "v0.2.0", Assets: app},
	}
	o := OffersFor(rels, "amoled175", "0.2.0")
	if o.Release != "0.3.0" || o.Pre != "0.4.0-rc1" || o.Main != "0.3.1-dev.4+abc1234" || !o.MainNewer || !o.Waiting {
		t.Fatalf("on 0.2.0: %+v", o)
	}
	o = OffersFor(rels, "amoled175", "0.3.1-dev.4+abc1234")
	if !o.OnMain || o.Main != "" || o.Release != "" || o.Pre != "0.4.0-rc1" {
		t.Fatalf("on the build from main: %+v", o)
	}
	o = OffersFor(rels, "amoled175", "0.3.1-dev.2+0000000")
	if o.Main == "" || !o.MainNewer || !o.Waiting {
		t.Fatalf("on an older build from main: %+v", o)
	}
	if o := OffersFor(rels[2:], "amoled175", "0.3.0"); o.Waiting || o.Release != "" {
		t.Fatalf("up to date: %+v", o)
	}
	if o := OffersFor(rels[:1], "amoled175", "0.3.0"); o.Waiting || o.Main == "" {
		t.Fatalf("a release user isn't nagged about builds from main: %+v", o)
	}
	if o := OffersFor(rels, "ws146", "0.2.0"); o != (Offers{}) {
		t.Fatalf("no images for that board: %+v", o)
	}
}
