package jobs

import (
	"errors"
	"testing"
	"time"
)

func wait(t *testing.T, js *Jobs, id string) Job {
	t.Helper()
	for range 200 {
		if j, _ := js.Get(id); j.State != "running" {
			return j
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatal("job did not finish")
	return Job{}
}

func TestJobs(t *testing.T) {
	js := &Jobs{}
	gate := make(chan struct{})
	j, err := js.Start("flash", "/dev/ttyACM0", func(r Report) error {
		r.Step("writing")
		r.Progress(5, 10)
		<-gate
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	if _, err := js.Start("flash", "/dev/ttyACM0", func(Report) error { return nil }); err == nil {
		t.Error("a second job for the same port must wait")
	}
	close(gate)
	done := wait(t, js, j.ID)
	if done.State != "done" || done.Total != 10 || len(done.Log) != 1 {
		t.Fatalf("job %+v", done)
	}
	j, _ = js.Start("update", "desk", func(Report) error { return errors.New("boom") })
	if failed := wait(t, js, j.ID); failed.State != "error" || failed.Error != "boom" {
		t.Fatalf("failed job %+v", failed)
	}
}
