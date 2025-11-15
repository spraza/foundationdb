package main

import (
	"encoding/json"
	"fmt"
	"sort"
	"strconv"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

// model holds the UI state for the Bubbletea application
type model struct {
	traceData         *TraceData
	currentTime       float64
	currentEventIndex int // Index of current event in traceData.Events
	clusterState      *ClusterState
	width             int
	height            int
	timeInputMode     bool
	timeInput         textinput.Model
	configViewMode    bool
	configScrollOffset int // Vertical scroll offset for config popup
}

// newModel creates a new model with the given trace data
func newModel(traceData *TraceData) model {
	ti := textinput.New()
	ti.Placeholder = "Enter time in seconds (e.g., 123.456)"
	ti.CharLimit = 20
	ti.Width = 40

	return model{
		traceData:          traceData,
		currentTime:        0.0,
		currentEventIndex:  0,
		clusterState:       NewClusterState(),
		timeInputMode:      false,
		timeInput:          ti,
		configViewMode:     false,
		configScrollOffset: 0,
	}
}

// Init initializes the model (required by Bubbletea)
func (m model) Init() tea.Cmd {
	return nil
}

// Update handles messages and updates the model (required by Bubbletea)
func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	var cmd tea.Cmd

	// Handle config view mode
	if m.configViewMode {
		switch msg := msg.(type) {
		case tea.KeyMsg:
			switch msg.String() {
			case "q", "c", "ctrl+c":
				// Exit config view mode
				m.configViewMode = false
				m.configScrollOffset = 0 // Reset scroll when exiting
				return m, nil
			case "up":
				// Scroll up in config view
				if m.configScrollOffset > 0 {
					m.configScrollOffset--
				}
				return m, nil
			case "down":
				// Scroll down in config view
				m.configScrollOffset++
				// Will be clamped in renderConfigPopup
				return m, nil
			}
		}
		return m, nil
	}

	// Handle time input mode separately
	if m.timeInputMode {
		switch msg := msg.(type) {
		case tea.KeyMsg:
			switch msg.String() {
			case "enter":
				// Try to parse the time and jump to it
				if timeStr := m.timeInput.Value(); timeStr != "" {
					if targetTime, err := strconv.ParseFloat(timeStr, 64); err == nil {
						// Only jump if within valid range
						if targetTime >= m.traceData.MinTime && targetTime <= m.traceData.MaxTime {
							m.currentEventIndex = m.traceData.GetEventIndexAtTime(targetTime)
							m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
							m.updateClusterState()
							// Exit time input mode
							m.timeInputMode = false
							m.timeInput.Reset()
						}
						// If invalid, stay in input mode so user can see the error and correct it
						return m, nil
					}
				}
				// If empty or parse error, stay in input mode
				return m, nil

			case "esc", "ctrl+c", "q", "t":
				// Cancel time input
				m.timeInputMode = false
				m.timeInput.Reset()
				return m, nil
			}
		}

		// Update the text input
		m.timeInput, cmd = m.timeInput.Update(msg)
		return m, cmd
	}

	// Normal mode (not in time input)
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "ctrl+c", "q", "Q":
			return m, tea.Quit

		case "t":
			// Enter time input mode
			m.timeInputMode = true
			m.timeInput.Focus()
			return m, textinput.Blink

		case "c":
			// Enter config view mode
			m.configViewMode = true
			return m, nil

		case "ctrl+n":
			// Move forward to next event
			if m.currentEventIndex < len(m.traceData.Events)-1 {
				m.currentEventIndex++
				m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
				m.updateClusterState()
			}

		case "ctrl+p":
			// Move backward to previous event
			if m.currentEventIndex > 0 {
				m.currentEventIndex--
				m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
				m.updateClusterState()
			}

		case "right":
			// Fast forward (1 second)
			newTime := m.currentTime + 1.0
			if newTime <= m.traceData.MaxTime {
				m.currentEventIndex = m.traceData.GetEventIndexAtTime(newTime)
				m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
				m.updateClusterState()
			}

		case "left":
			// Fast backward (1 second)
			newTime := m.currentTime - 1.0
			if newTime >= m.traceData.MinTime {
				m.currentEventIndex = m.traceData.GetEventIndexAtTime(newTime)
				m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
				m.updateClusterState()
			}

		case "g":
			// Jump to start (like less)
			m.currentEventIndex = 0
			m.currentTime = m.traceData.MinTime
			m.updateClusterState()

		case "G", "shift+g":
			// Jump to end (like less)
			m.currentEventIndex = len(m.traceData.Events) - 1
			m.currentTime = m.traceData.MaxTime
			m.updateClusterState()

		case "r":
			// Jump backward to latest MasterRecoveryState with StatusCode="0"
			if recovery := m.traceData.FindPreviousRecoveryWithStatusCode(m.currentEventIndex, "0"); recovery != nil {
				m.currentEventIndex = recovery.EventIndex
				m.currentTime = recovery.Time
				m.updateClusterState()
			}

		case "R", "shift+r":
			// Jump forward to earliest MasterRecoveryState with StatusCode="0"
			if recovery := m.traceData.FindNextRecoveryWithStatusCode(m.currentEventIndex, "0"); recovery != nil {
				m.currentEventIndex = recovery.EventIndex
				m.currentTime = recovery.Time
				m.updateClusterState()
			}
		}

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
	}

	return m, nil
}

// formatTraceEvent formats a trace event for display with config.fish field ordering and colors
func formatTraceEvent(event *TraceEvent, isCurrent bool) string {
	// Skip fields as per config.fish and fields shown in topology
	skipFields := map[string]bool{
		"DateTime":         true,
		"ThreadID":         true,
		"LogGroup":         true,
		"TrackLatestType":  true,
		"Roles":            true, // Shown in topology
	}

	// Color styles
	fieldNameStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("240")) // Dim
	fieldValueStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("46"))  // Green
	currentLineStyle := lipgloss.NewStyle().Background(lipgloss.Color("58")) // Dark yellowish highlight

	var parts []string

	// Add fields in specific order: Time, Type, Severity (skip Machine, Roles, ID - shown in topology), then other attributes
	if event.Time != "" {
		parts = append(parts, fieldNameStyle.Render("Time=")+fieldValueStyle.Render(event.Time))
	}
	if event.Type != "" {
		parts = append(parts, fieldNameStyle.Render("Type=")+fieldValueStyle.Render(event.Type))
	}
	if event.Severity != "" {
		parts = append(parts, fieldNameStyle.Render("Severity=")+fieldValueStyle.Render(event.Severity))
	}
	// Skip Machine - shown in topology
	// Skip Roles - shown in topology
	// Skip ID - shown in topology

	// Add remaining attributes (sorted for consistent ordering)
	var attrKeys []string
	for key := range event.Attrs {
		if key == "Roles" {
			continue // Already handled
		}
		if skipFields[key] {
			continue
		}
		attrKeys = append(attrKeys, key)
	}
	// Sort keys for deterministic ordering
	sort.Strings(attrKeys)

	for _, key := range attrKeys {
		value := event.Attrs[key]
		parts = append(parts, fieldNameStyle.Render(key+"=")+lipgloss.NewStyle().Foreground(lipgloss.Color("252")).Render(value))
	}

	line := strings.Join(parts, " ")
	if isCurrent {
		return currentLineStyle.Render(line)
	}
	return line
}


func (m *model) updateClusterState() {
	// Get events up to and including the current event index
	events := m.traceData.Events[:m.currentEventIndex+1]
	m.clusterState = BuildClusterState(events)
}

// buildEventListPane builds the event list pane showing events around current time
func (m model) buildEventListPane(availableHeight int, paneWidth int) []string {
	var lines []string
	currentIdx := m.currentEventIndex

	// We need to center based on LINE count, not event count
	// First, render the current event to see how many lines it takes
	currentEvent := &m.traceData.Events[currentIdx]
	currentEventLine := formatTraceEvent(currentEvent, false)
	currentWrappedLines := wrapText(currentEventLine, paneWidth)
	currentEventLineCount := len(currentWrappedLines)

	// Calculate how many lines we want above and below current event
	targetLinesAbove := availableHeight / 2

	// Build lines going backwards from current event
	var linesAbove []string
	lineCount := 0
	for i := currentIdx - 1; i >= 0 && lineCount < targetLinesAbove; i-- {
		event := &m.traceData.Events[i]
		eventLine := formatTraceEvent(event, false)
		wrappedLines := wrapText(eventLine, paneWidth)

		// Check if adding this event would exceed our target
		if lineCount+len(wrappedLines) > targetLinesAbove {
			break
		}

		// Prepend to linesAbove (we're going backwards)
		for j := len(wrappedLines) - 1; j >= 0; j-- {
			linesAbove = append([]string{wrappedLines[j]}, linesAbove...)
		}
		lineCount += len(wrappedLines)
	}

	// Add lines above
	lines = append(lines, linesAbove...)

	// Add current event with highlight (only first line)
	highlightStyle := lipgloss.NewStyle().Background(lipgloss.Color("58"))
	for i, line := range currentWrappedLines {
		if i == 0 {
			// Highlight only the first line (where Time= appears)
			lines = append(lines, highlightStyle.Render(line))
		} else {
			// Subsequent wrapped lines are not highlighted
			lines = append(lines, line)
		}
	}

	// Build lines going forwards from current event
	lineCount = len(linesAbove) + currentEventLineCount
	for i := currentIdx + 1; i < len(m.traceData.Events) && lineCount < availableHeight; i++ {
		event := &m.traceData.Events[i]
		eventLine := formatTraceEvent(event, false)
		wrappedLines := wrapText(eventLine, paneWidth)

		// Check if adding this event would exceed available height
		if lineCount+len(wrappedLines) > availableHeight {
			break
		}

		for _, line := range wrappedLines {
			lines = append(lines, line)
		}
		lineCount += len(wrappedLines)
	}

	return lines
}

// wrapText wraps text to fit within the specified width, preserving ANSI color codes
func wrapText(text string, width int) []string {
	if width <= 0 {
		return []string{text}
	}

	// Use lipgloss to wrap text
	wrapStyle := lipgloss.NewStyle().Width(width)
	wrapped := wrapStyle.Render(text)

	// Split into lines
	lines := strings.Split(wrapped, "\n")
	return lines
}


// View renders the UI (required by Bubbletea)
func (m model) View() string {
	// Get current event's machine and ID for highlighting in topology
	var currentMachine string
	var currentID string
	if m.currentEventIndex >= 0 && m.currentEventIndex < len(m.traceData.Events) {
		currentMachine = m.traceData.Events[m.currentEventIndex].Machine
		currentID = m.traceData.Events[m.currentEventIndex].ID
	}

	// Calculate available height for topology (reserve space for config, recovery, scrubber, help)
	// Bottom section (with borders and padding):
	//   - configStyle border + padding + content = 3 lines
	//   - recovery line = 1 line
	//   - scrubberStyle border + padding + content = 3 lines
	//   - help line = 1 line
	//   Total bottom = 8 lines
	availableHeight := m.height - 8
	if availableHeight < 1 {
		availableHeight = 1 // Minimum 1 line
	}

	// Styles
	dcHeaderStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("33")).
		Underline(true).
		MarginTop(0).
		MarginBottom(0)

	testerHeaderStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("135")).
		Underline(true).
		MarginTop(0).
		MarginBottom(0)

	workerStyleGray := lipgloss.NewStyle().
		Foreground(lipgloss.Color("240")).
		PaddingLeft(2)

	workerStyleGreen := lipgloss.NewStyle().
		Foreground(lipgloss.Color("46")).
		Bold(true).
		PaddingLeft(2)

	// Style for current machine (event source) - cyan with arrow
	workerStyleCurrent := lipgloss.NewStyle().
		Foreground(lipgloss.Color("51")).
		Bold(true).
		PaddingLeft(0)

	roleStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("252")) // Normal gray color

	// Style for current role (when ID matches)
	roleStyleCurrent := lipgloss.NewStyle().
		Foreground(lipgloss.Color("51")).
		Bold(true)

	scrubberStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("241")).
		PaddingLeft(1)

	helpStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("241"))

	// Get workers grouped by DC and testers
	dcWorkers := m.clusterState.GetWorkersByDC()
	testers := m.clusterState.GetTesters()

	// Build all topology lines first (before packing into columns)
	var allTopologyLines []string

	if len(dcWorkers) == 0 && len(testers) == 0 {
		allTopologyLines = append(allTopologyLines, "")
		allTopologyLines = append(allTopologyLines, "  <NO_CLUSTER_YET>")
	} else {
		// Display main machines grouped by DC
		if len(dcWorkers) > 0 {
			// Sort DC IDs for consistent display
			dcIDs := make([]string, 0, len(dcWorkers))
			for dcID := range dcWorkers {
				dcIDs = append(dcIDs, dcID)
			}
			// Simple sort
			for i := 0; i < len(dcIDs); i++ {
				for j := i + 1; j < len(dcIDs); j++ {
					if dcIDs[i] > dcIDs[j] {
						dcIDs[i], dcIDs[j] = dcIDs[j], dcIDs[i]
					}
				}
			}

			// Display each DC
			for _, dcID := range dcIDs {
				workers := dcWorkers[dcID]
				allTopologyLines = append(allTopologyLines, dcHeaderStyle.Render(fmt.Sprintf("DC %s", dcID)))

				for _, worker := range workers {
					// Check if this worker's machine matches current event
					isCurrentMachine := worker.Machine == currentMachine

					// Check if any role ID matches current event ID
					var matchingRole *RoleInfo
					if isCurrentMachine && currentID != "" {
						for i := range worker.Roles {
							if worker.Roles[i].ID == currentID {
								matchingRole = &worker.Roles[i]
								break
							}
						}
					}

					// Display machine address at top level (no ID here)
					if matchingRole != nil {
						// Role ID matches - show machine normally, will highlight role below
						if worker.HasNonWorkerRoles() {
							workerLine := fmt.Sprintf("● %s", worker.Machine)
							allTopologyLines = append(allTopologyLines, workerStyleGreen.Render(workerLine))
						} else {
							workerLine := fmt.Sprintf("● %s", worker.Machine)
							allTopologyLines = append(allTopologyLines, workerStyleGray.Render(workerLine))
						}

						// Show each role, highlighting the one with matching ID
						for _, role := range worker.Roles {
							roleLabel := role.Name
							if role.ID != "" {
								roleLabel = fmt.Sprintf("%s [%s]", role.Name, role.ID)
							}
							if role.ID == currentID {
								// Highlight this specific role
								allTopologyLines = append(allTopologyLines, roleStyleCurrent.Render("    → "+roleLabel))
							} else {
								allTopologyLines = append(allTopologyLines, roleStyle.Render("      "+roleLabel))
							}
						}
					} else if isCurrentMachine {
						// Machine matches but no role ID match - highlight machine
						workerLine := fmt.Sprintf("→ ● %s", worker.Machine)
						allTopologyLines = append(allTopologyLines, workerStyleCurrent.Render(workerLine))

						// Show all roles normally
						for _, role := range worker.Roles {
							roleLabel := role.Name
							if role.ID != "" {
								roleLabel = fmt.Sprintf("%s [%s]", role.Name, role.ID)
							}
							allTopologyLines = append(allTopologyLines, roleStyle.Render("      "+roleLabel))
						}
					} else if worker.HasNonWorkerRoles() {
						// Normal worker with roles (no match)
						workerLine := fmt.Sprintf("● %s", worker.Machine)
						allTopologyLines = append(allTopologyLines, workerStyleGreen.Render(workerLine))

						for _, role := range worker.Roles {
							roleLabel := role.Name
							if role.ID != "" {
								roleLabel = fmt.Sprintf("%s [%s]", role.Name, role.ID)
							}
							allTopologyLines = append(allTopologyLines, roleStyle.Render("      "+roleLabel))
						}
					} else {
						// Worker without roles OR only Worker role (no match)
						workerLine := fmt.Sprintf("● %s", worker.Machine)
						allTopologyLines = append(allTopologyLines, workerStyleGray.Render(workerLine))

						// Show all roles (including Worker if present)
						for _, role := range worker.Roles {
							roleLabel := role.Name
							if role.ID != "" {
								roleLabel = fmt.Sprintf("%s [%s]", role.Name, role.ID)
							}
							allTopologyLines = append(allTopologyLines, roleStyle.Render("      "+roleLabel))
						}
					}
				}
			}
		}

		// Display testers in a separate section
		if len(testers) > 0 {
			allTopologyLines = append(allTopologyLines, testerHeaderStyle.Render("Testers"))

			for _, worker := range testers {
				// Check if this tester's machine matches current event
				isCurrentMachine := worker.Machine == currentMachine

				// Check if any role ID matches current event ID
				var matchingRole *RoleInfo
				if isCurrentMachine && currentID != "" {
					for i := range worker.Roles {
						if worker.Roles[i].ID == currentID {
							matchingRole = &worker.Roles[i]
							break
						}
					}
				}

				// Display machine address at top level (no ID here)
				if matchingRole != nil {
					// Role ID matches - show machine normally, will highlight role below
					if worker.HasNonWorkerRoles() {
						workerLine := fmt.Sprintf("● %s", worker.Machine)
						allTopologyLines = append(allTopologyLines, workerStyleGreen.Render(workerLine))
					} else {
						workerLine := fmt.Sprintf("● %s", worker.Machine)
						allTopologyLines = append(allTopologyLines, workerStyleGray.Render(workerLine))
					}

					// Show each role, highlighting the one with matching ID
					for _, role := range worker.Roles {
						roleLabel := role.Name
						if role.ID != "" {
							roleLabel = fmt.Sprintf("%s [%s]", role.Name, role.ID)
						}
						if role.ID == currentID {
							// Highlight this specific role
							allTopologyLines = append(allTopologyLines, roleStyleCurrent.Render("    → "+roleLabel))
						} else {
							allTopologyLines = append(allTopologyLines, roleStyle.Render("      "+roleLabel))
						}
					}
				} else if isCurrentMachine {
					// Machine matches but no role ID match - highlight machine
					workerLine := fmt.Sprintf("→ ● %s", worker.Machine)
					allTopologyLines = append(allTopologyLines, workerStyleCurrent.Render(workerLine))

					// Show all roles normally
					for _, role := range worker.Roles {
						roleLabel := role.Name
						if role.ID != "" {
							roleLabel = fmt.Sprintf("%s [%s]", role.Name, role.ID)
						}
						allTopologyLines = append(allTopologyLines, roleStyle.Render("      "+roleLabel))
					}
				} else if worker.HasNonWorkerRoles() {
					// Normal tester with roles (no match)
					workerLine := fmt.Sprintf("● %s", worker.Machine)
					allTopologyLines = append(allTopologyLines, workerStyleGreen.Render(workerLine))

					for _, role := range worker.Roles {
						roleLabel := role.Name
						if role.ID != "" {
							roleLabel = fmt.Sprintf("%s [%s]", role.Name, role.ID)
						}
						allTopologyLines = append(allTopologyLines, roleStyle.Render("      "+roleLabel))
					}
				} else {
					// Tester without roles OR only Worker role (no match)
					workerLine := fmt.Sprintf("● %s", worker.Machine)
					allTopologyLines = append(allTopologyLines, workerStyleGray.Render(workerLine))

					// Show all roles (including Worker if present)
					for _, role := range worker.Roles {
						roleLabel := role.Name
						if role.ID != "" {
							roleLabel = fmt.Sprintf("%s [%s]", role.Name, role.ID)
						}
						allTopologyLines = append(allTopologyLines, roleStyle.Render("      "+roleLabel))
					}
				}
			}
		}
	}

	// Pack lines into columns based on available height
	// Keep machines and their roles together (don't split across columns)
	var columns [][]string
	if len(allTopologyLines) <= availableHeight {
		// Everything fits in one column
		columns = [][]string{allTopologyLines}
	} else {
		// Need multiple columns - group lines to keep machines together
		currentColumn := []string{}
		i := 0
		for i < len(allTopologyLines) {
			line := allTopologyLines[i]

			// Check if this is a DC header or machine line (starts with "DC", "Testers", "●", or "→")
			// These mark the start of a new logical group
			isGroupStart := strings.HasPrefix(line, "DC ") ||
				strings.HasPrefix(line, "Testers") ||
				strings.Contains(line, "● ") ||
				strings.Contains(line, "→ ●")

			if isGroupStart && len(currentColumn) > 0 {
				// Peek ahead to see how many lines this group needs
				groupSize := 1 // Current line
				for j := i + 1; j < len(allTopologyLines); j++ {
					nextLine := allTopologyLines[j]
					// Check if next line is a role (indented) or another group start
					if strings.HasPrefix(nextLine, "DC ") ||
						strings.HasPrefix(nextLine, "Testers") ||
						strings.Contains(nextLine, "● ") ||
						strings.Contains(nextLine, "→ ●") {
						break // End of this group
					}
					groupSize++
				}

				// If adding this group would overflow, start new column
				if len(currentColumn) + groupSize > availableHeight {
					columns = append(columns, currentColumn)
					currentColumn = []string{}
				}
			}

			currentColumn = append(currentColumn, line)
			i++
		}
		// Add the last column
		if len(currentColumn) > 0 {
			columns = append(columns, currentColumn)
		}
	}

	// Calculate max width for each column
	columnWidths := make([]int, len(columns))
	for colIdx, column := range columns {
		maxWidth := 0
		for _, line := range column {
			width := lipgloss.Width(line)
			if width > maxWidth {
				maxWidth = width
			}
		}
		columnWidths[colIdx] = maxWidth + 2 // Add 2 for spacing between columns
	}

	// Calculate total left pane width
	leftPaneWidth := 0
	for _, w := range columnWidths {
		leftPaneWidth += w
	}
	if leftPaneWidth < 20 {
		leftPaneWidth = 20
	}
	// Don't take more than 60% of screen
	maxLeftWidth := (m.width * 3) / 5
	if leftPaneWidth > maxLeftWidth {
		leftPaneWidth = maxLeftWidth
	}

	// Calculate right pane width (rest of screen minus border)
	rightPaneWidth := m.width - leftPaneWidth - 3 // 3 for " │ "
	if rightPaneWidth < 30 {
		rightPaneWidth = 30
	}

	// Build right pane (event list) content as array of lines
	eventLines := m.buildEventListPane(availableHeight, rightPaneWidth)

	// Pad columns to same height
	maxLines := availableHeight
	for i := range columns {
		for len(columns[i]) < maxLines {
			columns[i] = append(columns[i], "")
		}
	}
	for len(eventLines) < maxLines {
		eventLines = append(eventLines, "")
	}

	// Build split view line by line with columnar topology
	var splitContent strings.Builder
	borderStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("240"))

	for lineIdx := 0; lineIdx < maxLines; lineIdx++ {
		// Render all topology columns for this line
		for colIdx, column := range columns {
			var line string
			if lineIdx < len(column) {
				line = column[lineIdx]
			}

			// Pad to column width
			lineWidth := lipgloss.Width(line)
			targetWidth := columnWidths[colIdx]
			if lineWidth < targetWidth {
				line = line + strings.Repeat(" ", targetWidth-lineWidth)
			}

			splitContent.WriteString(line)
		}

		// Add border and event line
		splitContent.WriteString(borderStyle.Render(" │ "))

		if lineIdx < len(eventLines) {
			splitContent.WriteString(eventLines[lineIdx])
		}

		splitContent.WriteString("\n")
	}

	// Build bottom section (spans full width)
	var bottomSection strings.Builder

	// Add separator
	separator := strings.Repeat("─", m.width)
	bottomSection.WriteString(lipgloss.NewStyle().Foreground(lipgloss.Color("240")).Render(separator))
	bottomSection.WriteString("\n")

	// DB Configuration section
	config := m.traceData.GetLatestConfigAtTime(m.currentTime)
	if config != nil {
		configStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("243")).
			PaddingLeft(1)

		configTitleStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("39")).
			Bold(true)

		configValueStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("252"))

		configContent := configTitleStyle.Render(fmt.Sprintf("DB Config (t=%.2fs)", config.Time)) + " "

		// Build compact config display with exact field names
		configParts := []string{}
		if config.RedundancyMode != "" {
			configParts = append(configParts, fmt.Sprintf("redundancy_mode=%s", config.RedundancyMode))
		}
		if config.UsableRegions > 0 {
			configParts = append(configParts, fmt.Sprintf("usable_regions=%d", config.UsableRegions))
		}
		if config.Logs > 0 {
			configParts = append(configParts, fmt.Sprintf("logs=%d", config.Logs))
		}
		if config.LogRouters > 0 {
			configParts = append(configParts, fmt.Sprintf("log_routers=%d", config.LogRouters))
		}
		if config.RemoteLogs > 0 {
			configParts = append(configParts, fmt.Sprintf("remote_logs=%d", config.RemoteLogs))
		}
		if config.Proxies > 0 {
			configParts = append(configParts, fmt.Sprintf("proxies=%d", config.Proxies))
		}
		if config.GrvProxies > 0 {
			configParts = append(configParts, fmt.Sprintf("grv_proxies=%d", config.GrvProxies))
		}
		if config.BackupWorkerEnabled > 0 {
			configParts = append(configParts, fmt.Sprintf("backup_worker_enabled=%d", config.BackupWorkerEnabled))
		}
		if config.StorageEngine != "" {
			configParts = append(configParts, fmt.Sprintf("storage_engine=%s", config.StorageEngine))
		}

		configContent += configValueStyle.Render(strings.Join(configParts, " | "))
		bottomSection.WriteString(configStyle.Render(configContent))
		bottomSection.WriteString("\n")
	}

	// Recovery State section
	recoveryState := m.traceData.GetLatestRecoveryStateAtIndex(m.currentEventIndex)
	if recoveryState != nil {
		recoveryStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("243")).
			PaddingLeft(1)

		recoveryTitleStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("39")).
			Bold(true)

		// Color code based on StatusCode value
		var recoveryValueStyle lipgloss.Style
		if statusCode, err := strconv.Atoi(recoveryState.StatusCode); err == nil {
			if statusCode < 11 {
				// Red for < 11
				recoveryValueStyle = lipgloss.NewStyle().Foreground(lipgloss.Color("196"))
			} else if statusCode >= 11 && statusCode < 14 {
				// Blue for 11 <= statusCode < 14
				recoveryValueStyle = lipgloss.NewStyle().Foreground(lipgloss.Color("39"))
			} else if statusCode == 14 {
				// Green for = 14
				recoveryValueStyle = lipgloss.NewStyle().Foreground(lipgloss.Color("46"))
			} else {
				// Default gray for > 14
				recoveryValueStyle = lipgloss.NewStyle().Foreground(lipgloss.Color("252"))
			}
		} else {
			// Default gray if can't parse
			recoveryValueStyle = lipgloss.NewStyle().Foreground(lipgloss.Color("252"))
		}

		recoveryContent := recoveryTitleStyle.Render(fmt.Sprintf("Recovery State (t=%.6fs)", recoveryState.Time)) + " "
		recoveryContent += recoveryValueStyle.Render(fmt.Sprintf("StatusCode=%s | Status=%s", recoveryState.StatusCode, recoveryState.Status))

		bottomSection.WriteString(recoveryStyle.Render(recoveryContent))
		bottomSection.WriteString("\n")
	}

	// Time scrubber
	separatorStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("240"))
	bottomSection.WriteString(separatorStyle.Render(strings.Repeat("─", 20)))
	bottomSection.WriteString("\n")
	scrubberContent := fmt.Sprintf("Time: %.6fs", m.currentTime)
	bottomSection.WriteString(scrubberStyle.Render(scrubberContent))
	bottomSection.WriteString("\n")

	// Help text
	help := helpStyle.Render("Ctrl+N/P: next/prev event | Left/Right: ±1s | g/G: start/end | t: jump time | r/R: recovery | c: config | q: quit")
	bottomSection.WriteString(help)

	// Combine split view with bottom section
	fullView := splitContent.String() + bottomSection.String()

	// If in config view mode, show config popup overlay
	if m.configViewMode {
		config := m.traceData.GetLatestConfigAtTime(m.currentTime)
		if config != nil {
			return m.renderConfigPopup(fullView, config)
		} else {
			// No config available yet - show message
			return m.renderNoConfigPopup(fullView)
		}
	}

	// If in time input mode, show popup overlay
	if m.timeInputMode {
		return m.renderTimeInputPopup(fullView)
	}

	return fullView
}

// renderNoConfigPopup renders a message when no config is available
func (m model) renderNoConfigPopup(baseView string) string {
	popupStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("39")).
		Padding(1, 2).
		Width(50)

	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("39"))

	messageStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("243")).
		MarginTop(1)

	helpStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("241")).
		MarginTop(1)

	popupContent := titleStyle.Render("DB Config") + "\n" +
		messageStyle.Render("No configuration available yet at this time.") + "\n" +
		helpStyle.Render("Press q or c to close")

	popup := popupStyle.Render(popupContent)

	// Overlay the popup on top of the base view
	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, popup, lipgloss.WithWhitespaceChars(" "))
}

// renderConfigPopup renders the full config JSON popup overlay
func (m model) renderConfigPopup(baseView string, config *DBConfig) string {
	popupStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("39")).
		Padding(1, 2).
		MaxWidth(m.width - 10).
		MaxHeight(m.height - 4)

	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("39"))

	helpStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("241")).
		MarginTop(1)

	jsonStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("252"))

	scrollIndicatorStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("240")).
		Italic(true)

	// Pretty-print the JSON
	jsonBytes, err := json.MarshalIndent(config.RawJSON, "", "  ")
	var jsonContent string
	if err != nil {
		jsonContent = "Error formatting JSON"
	} else {
		jsonContent = string(jsonBytes)
	}

	// Split JSON into lines
	jsonLines := strings.Split(jsonContent, "\n")
	totalLines := len(jsonLines)

	// Calculate available height for JSON content
	// Account for: title (1 line) + top margin (1) + help text (1) + bottom margin (1) + padding (2) + border (2) = 8 lines
	maxContentHeight := m.height - 12
	if maxContentHeight < 5 {
		maxContentHeight = 5 // Minimum visible lines
	}

	// Clamp scroll offset
	maxScrollOffset := totalLines - maxContentHeight
	if maxScrollOffset < 0 {
		maxScrollOffset = 0
	}

	displayScrollOffset := m.configScrollOffset
	if displayScrollOffset < 0 {
		displayScrollOffset = 0
	}
	if displayScrollOffset > maxScrollOffset {
		displayScrollOffset = maxScrollOffset
	}

	// Determine if we have more content above/below
	hasMoreAbove := displayScrollOffset > 0
	hasMoreBelow := displayScrollOffset < maxScrollOffset

	// Calculate visible window
	visibleLines := jsonLines
	if totalLines > maxContentHeight {
		endIdx := displayScrollOffset + maxContentHeight
		if endIdx > totalLines {
			endIdx = totalLines
		}
		visibleLines = jsonLines[displayScrollOffset:endIdx]
	}

	// Build JSON content with scroll indicators
	var jsonContentBuilder strings.Builder

	if hasMoreAbove {
		jsonContentBuilder.WriteString(scrollIndicatorStyle.Render("↑ more above"))
		jsonContentBuilder.WriteString("\n")
	}

	jsonContentBuilder.WriteString(jsonStyle.Render(strings.Join(visibleLines, "\n")))

	if hasMoreBelow {
		jsonContentBuilder.WriteString("\n")
		jsonContentBuilder.WriteString(scrollIndicatorStyle.Render("↓ more below"))
	}

	// Build popup content
	popupContent := titleStyle.Render(fmt.Sprintf("DB Config (t=%.2fs)", config.Time)) + "\n\n" +
		jsonContentBuilder.String() + "\n\n" +
		helpStyle.Render("Press q or c to close | Up/Down to scroll")

	popup := popupStyle.Render(popupContent)

	// Overlay the popup on top of the base view
	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, popup, lipgloss.WithWhitespaceChars(" "))
}

// renderTimeInputPopup renders the time input popup overlay
func (m model) renderTimeInputPopup(baseView string) string {
	popupStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("39")).
		Padding(1, 2).
		Width(50)

	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("39"))

	helpStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("241")).
		MarginTop(1)

	errorStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("196")).
		MarginTop(1)

	rangeStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("243")).
		Italic(true)

	// Validate the current input
	var validationMsg string
	if inputValue := m.timeInput.Value(); inputValue != "" {
		if targetTime, err := strconv.ParseFloat(inputValue, 64); err != nil {
			validationMsg = errorStyle.Render("✗ Invalid number format")
		} else if targetTime < m.traceData.MinTime {
			validationMsg = errorStyle.Render(fmt.Sprintf("✗ Time must be >= %.2f", m.traceData.MinTime))
		} else if targetTime > m.traceData.MaxTime {
			validationMsg = errorStyle.Render(fmt.Sprintf("✗ Time must be <= %.2f", m.traceData.MaxTime))
		} else {
			validationMsg = lipgloss.NewStyle().Foreground(lipgloss.Color("46")).Render("✓ Valid")
		}
	}

	rangeInfo := rangeStyle.Render(fmt.Sprintf("Valid range: %.2f - %.2f seconds", m.traceData.MinTime, m.traceData.MaxTime))

	popupContent := titleStyle.Render("Jump to Time") + "\n\n" +
		m.timeInput.View() + "\n"

	if validationMsg != "" {
		popupContent += validationMsg + "\n"
	}

	popupContent += "\n" + rangeInfo + "\n" +
		helpStyle.Render("Enter: jump | Esc/q/t: cancel")

	popup := popupStyle.Render(popupContent)

	// Overlay the popup on top of the base view
	// Place it roughly in the center
	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, popup, lipgloss.WithWhitespaceChars(" "))
}

// runUI starts the Bubbletea TUI program
func runUI(traceData *TraceData) error {
	p := tea.NewProgram(
		newModel(traceData),
		tea.WithAltScreen(),
	)

	if _, err := p.Run(); err != nil {
		return fmt.Errorf("error running UI: %w", err)
	}

	return nil
}
