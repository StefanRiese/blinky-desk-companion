// Package jobs runs long tasks of buddy-link in the background (flashing a board,
// updating a device's firmware) and keeps their progress for the web UI and buddy-ctl.
// One job at a time per target (a port or a device); finished jobs are kept for an hour.
package jobs

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"sync"
	"time"

	"github.com/StefanRiese/blinky-desk-companion/host/internal/events"
)

// Job is a task's state.
type Job struct {
	ID      string    `json:"id"`
	Kind    string    `json:"kind"`   // flash | update
	Target  string    `json:"target"` // port or device id
	State   string    `json:"state"`  // running | done | error
	Step    string    `json:"step"`   // what it does now, for the user
	Done    int       `json:"done"`   // bytes or steps done
	Total   int       `json:"total"`
	Error   string    `json:"error,omitempty"`
	Log     []string  `json:"log"`
	Started time.Time `json:"started"`
	Ended   time.Time `json:"ended,omitzero"`
}

// Report is what a running job tells about itself.
type Report struct {
	jobs *Jobs
	id   string
}

// Step sets the current step (and adds it to the log).
func (r Report) Step(s string) {
	r.jobs.update(r.id, func(j *Job) { j.Step = s; j.Log = append(j.Log, s) })
}

// Progress sets how much is done.
func (r Report) Progress(done, total int) {
	r.jobs.update(r.id, func(j *Job) { j.Done, j.Total = done, total })
}

// Jobs keeps the jobs.
type Jobs struct {
	Bus *events.Bus // gets a "job" event on every change (buddy-ctl monitor); may be nil

	mu sync.Mutex
	m  map[string]*Job
}

// keepFinished is how long a finished job stays readable.
const keepFinished = time.Hour

// Start runs f as a new job, unless a job for target is still running.
func (js *Jobs) Start(kind, target string, f func(r Report) error) (Job, error) {
	js.mu.Lock()
	if js.m == nil {
		js.m = map[string]*Job{}
	}
	for id, j := range js.m {
		if j.State == "running" && j.Target == target {
			js.mu.Unlock()
			return Job{}, fmt.Errorf("%s is busy with %s job %s", target, j.Kind, j.ID)
		}
		if j.State != "running" && time.Since(j.Ended) > keepFinished {
			delete(js.m, id)
		}
	}
	b := make([]byte, 6)
	rand.Read(b)
	j := &Job{ID: hex.EncodeToString(b), Kind: kind, Target: target, State: "running", Started: time.Now(), Log: []string{}}
	js.m[j.ID] = j
	snap := *j
	js.mu.Unlock()

	go func() {
		err := f(Report{jobs: js, id: j.ID})
		js.update(j.ID, func(j *Job) {
			j.Ended = time.Now()
			if err != nil {
				j.State, j.Error = "error", err.Error()
				j.Log = append(j.Log, "error: "+err.Error())
			} else {
				j.State, j.Step = "done", "done"
			}
		})
	}()
	return snap, nil
}

// Get returns a copy of the job id.
func (js *Jobs) Get(id string) (Job, bool) {
	js.mu.Lock()
	defer js.mu.Unlock()
	j, ok := js.m[id]
	if !ok {
		return Job{}, false
	}
	c := *j
	c.Log = append([]string(nil), j.Log...)
	return c, true
}

func (js *Jobs) update(id string, f func(*Job)) {
	js.mu.Lock()
	j, ok := js.m[id]
	if ok {
		f(j)
	}
	var snap Job
	if ok {
		snap = *j
		snap.Log = nil // events carry the state, not the whole log
	}
	js.mu.Unlock()
	if ok && js.Bus != nil {
		js.Bus.Publish("job", snap.Target, snap)
	}
}
