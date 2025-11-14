package main

import (
	"regexp"
	"strings"
)

// Worker represents a process in the cluster
type Worker struct {
	Machine     string   // e.g., "[abcd::2:1:1:0]:1"
	Roles       []string // e.g., ["Coordinator", "ClusterController"]
	MachineType string   // "main" or "tester"
	DCID        string   // e.g., "0", "1", "2", etc.
}

// ClusterState represents the state of the cluster at a given time
type ClusterState struct {
	Workers map[string]*Worker // Key: Machine address
}

// NewClusterState creates a new empty cluster state
func NewClusterState() *ClusterState {
	return &ClusterState{
		Workers: make(map[string]*Worker),
	}
}

// parseAddress extracts machine type and DC ID from address
// Format 1: [abcd::X:Y:Z:W]:Port where X=type (2=main, 3=tester), Y=DC ID
// Format 2: X.Y.Z.W:Port where X=type (2=main, 3=tester), Y=DC ID
func parseAddress(address string) (machineType string, dcID string) {
	// Default values
	machineType = "unknown"
	dcID = "unknown"

	// Try format 1: [abcd::2:1:1:0]:1
	re1 := regexp.MustCompile(`\[abcd::(\d+):(\d+):`)
	matches := re1.FindStringSubmatch(address)

	if len(matches) >= 3 {
		typeNum := matches[1]
		dcNum := matches[2]

		if typeNum == "2" {
			machineType = "main"
		} else if typeNum == "3" {
			machineType = "tester"
		}

		dcID = dcNum
		return machineType, dcID
	}

	// Try format 2: 2.0.1.3:1
	re2 := regexp.MustCompile(`^(\d+)\.(\d+)\.`)
	matches = re2.FindStringSubmatch(address)

	if len(matches) >= 3 {
		typeNum := matches[1]
		dcNum := matches[2]

		if typeNum == "2" {
			machineType = "main"
		} else if typeNum == "3" {
			machineType = "tester"
		}

		dcID = dcNum
		return machineType, dcID
	}

	return machineType, dcID
}

// BuildClusterState builds the cluster state from events up to a given time
func BuildClusterState(events []TraceEvent) *ClusterState {
	state := NewClusterState()

	for _, event := range events {
		if event.Type == "Role" && event.Machine != "0.0.0.0:0" {
			transition := event.Attrs["Transition"]
			roleName := event.Attrs["As"]

			// Skip if no role name
			if roleName == "" {
				continue
			}

			// Get or create worker
			worker, exists := state.Workers[event.Machine]
			if !exists {
				machineType, dcID := parseAddress(event.Machine)
				worker = &Worker{
					Machine:     event.Machine,
					Roles:       []string{},
					MachineType: machineType,
					DCID:        dcID,
				}
				state.Workers[event.Machine] = worker
			}

			// Skip "Worker" role from being added to roles list, but we still track the worker
			if roleName == "Worker" {
				continue
			}

			// Handle role transition for real roles (not "Worker")
			if transition == "Begin" {
				// Add role if not already present
				hasRole := false
				for _, r := range worker.Roles {
					if r == roleName {
						hasRole = true
						break
					}
				}
				if !hasRole {
					worker.Roles = append(worker.Roles, roleName)
				}
			} else if transition == "End" {
				// Remove role
				newRoles := []string{}
				for _, r := range worker.Roles {
					if r != roleName {
						newRoles = append(newRoles, r)
					}
				}
				worker.Roles = newRoles
			}
			// "Refresh" transitions don't change state, just skip them
		}
	}

	return state
}

// GetWorkersByDC returns workers grouped by DC ID (main machines only)
func (cs *ClusterState) GetWorkersByDC() map[string][]*Worker {
	dcMap := make(map[string][]*Worker)

	for _, w := range cs.Workers {
		if w.MachineType == "main" {
			dcMap[w.DCID] = append(dcMap[w.DCID], w)
		}
	}

	// Sort workers within each DC by machine address for consistent ordering
	for _, workers := range dcMap {
		for i := 0; i < len(workers); i++ {
			for j := i + 1; j < len(workers); j++ {
				if workers[i].Machine > workers[j].Machine {
					workers[i], workers[j] = workers[j], workers[i]
				}
			}
		}
	}

	return dcMap
}

// GetTesters returns all tester workers
func (cs *ClusterState) GetTesters() []*Worker {
	testers := []*Worker{}

	for _, w := range cs.Workers {
		if w.MachineType == "tester" {
			testers = append(testers, w)
		}
	}

	// Sort testers by machine address for consistent ordering
	for i := 0; i < len(testers); i++ {
		for j := i + 1; j < len(testers); j++ {
			if testers[i].Machine > testers[j].Machine {
				testers[i], testers[j] = testers[j], testers[i]
			}
		}
	}

	return testers
}

// HasRoles returns true if the worker has any roles assigned
func (w *Worker) HasRoles() bool {
	return len(w.Roles) > 0
}

// RolesString returns a comma-separated string of roles
func (w *Worker) RolesString() string {
	if len(w.Roles) == 0 {
		return ""
	}
	return strings.Join(w.Roles, ", ")
}
