package main

import (
	"encoding/json"
	"fmt"
	"regexp"
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
	helpViewMode      bool // Help popup mode
	searchMode        bool // Search input mode
	searchDirection   string // "forward" or "backward"
	searchInput       textinput.Model
	searchPattern     string // Current search pattern
	searchActive      bool // Whether search highlighting is active
	// Filter state
	filterViewMode       bool // Filter popup mode
	filterShowAll        bool // Whether "All" is checked (default true)
	filterList           []string // List of inclusive filter patterns
	filterInput          textinput.Model // Input for new filter
	filterSelectedIndex  int // Selected filter index (for navigation/deletion)
	filterInputActive    bool // Whether input field is active
}

// newModel creates a new model with the given trace data
func newModel(traceData *TraceData) model {
	ti := textinput.New()
	ti.Placeholder = "Enter time in seconds (e.g., 123.456)"
	ti.CharLimit = 20
	ti.Width = 40

	si := textinput.New()
	si.Placeholder = "Enter search pattern (use * for wildcard)"
	si.CharLimit = 100
	si.Width = 60

	fi := textinput.New()
	fi.Placeholder = "Enter filter pattern (e.g., Type=WorkerHealthMonitor or Role*TL)"
	fi.CharLimit = 100
	fi.Width = 70

	return model{
		traceData:          traceData,
		currentTime:        0.0,
		currentEventIndex:  0,
		clusterState:       NewClusterState(),
		timeInputMode:      false,
		timeInput:          ti,
		configViewMode:     false,
		configScrollOffset: 0,
		helpViewMode:       false,
		searchMode:         false,
		searchDirection:    "",
		searchInput:        si,
		searchPattern:      "",
		searchActive:       false,
		// Filter initialization
		filterViewMode:      false,
		filterShowAll:       true, // Default: show all events
		filterList:          []string{},
		filterInput:         fi,
		filterSelectedIndex: -1, // -1 means input field is selected
		filterInputActive:   false,
	}
}

// Init initializes the model (required by Bubbletea)
func (m model) Init() tea.Cmd {
	return nil
}

// Update handles messages and updates the model (required by Bubbletea)
func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	var cmd tea.Cmd

	// Handle filter view mode
	if m.filterViewMode {
		switch msg := msg.(type) {
		case tea.KeyMsg:
			// If in input mode, handle input field first for most keys
			if m.filterInputActive {
				switch msg.String() {
				case "enter":
					// Add new filter from input
					if filterText := m.filterInput.Value(); filterText != "" {
						m.filterList = append(m.filterList, filterText)
						m.filterInput.Reset()
						m.filterInput.Blur()
						m.filterInputActive = false
						m.filterSelectedIndex = len(m.filterList) - 1 // Select the newly added filter
					}
					return m, nil
				case "esc", "ctrl+c":
					// Cancel input mode
					m.filterInput.Reset()
					m.filterInput.Blur()
					m.filterInputActive = false
					return m, nil
				default:
					// Pass all other keys (including backspace and space) to input field
					m.filterInput, cmd = m.filterInput.Update(msg)
					return m, cmd
				}
			}

			// Handle keys when NOT in input mode
			switch msg.String() {
			case "q", "f", "esc", "ctrl+c":
				// Exit filter view mode
				m.filterViewMode = false
				m.filterInput.Reset()
				return m, nil

			case "space", " ":
				// Toggle "All" checkbox (handle both "space" and " ")
				m.filterShowAll = !m.filterShowAll
				return m, nil

			case "i":
				// Activate input field for new filter
				m.filterInputActive = true
				m.filterInput.Focus()
				return m, textinput.Blink

			case "ctrl+n":
				// Navigate to next filter in list
				if len(m.filterList) > 0 {
					m.filterSelectedIndex++
					if m.filterSelectedIndex >= len(m.filterList) {
						m.filterSelectedIndex = 0
					}
				}
				return m, nil

			case "ctrl+p":
				// Navigate to previous filter in list
				if len(m.filterList) > 0 {
					m.filterSelectedIndex--
					if m.filterSelectedIndex < 0 {
						m.filterSelectedIndex = len(m.filterList) - 1
					}
				}
				return m, nil

			case "backspace":
				// Delete selected filter
				if m.filterSelectedIndex >= 0 && m.filterSelectedIndex < len(m.filterList) {
					// Remove filter at selected index
					m.filterList = append(m.filterList[:m.filterSelectedIndex], m.filterList[m.filterSelectedIndex+1:]...)
					// Adjust selection
					if m.filterSelectedIndex >= len(m.filterList) {
						m.filterSelectedIndex = len(m.filterList) - 1
					}
				}
				return m, nil
			}
		}
		return m, nil
	}

	// Handle help view mode
	if m.helpViewMode {
		switch msg := msg.(type) {
		case tea.KeyMsg:
			switch msg.String() {
			case "q", "h", "esc", "ctrl+c":
				// Exit help view mode
				m.helpViewMode = false
				return m, nil
			}
		}
		return m, nil
	}

	// Handle config view mode
	if m.configViewMode {
		switch msg := msg.(type) {
		case tea.KeyMsg:
			switch msg.String() {
			case "q", "c", "esc", "ctrl+c":
				// Exit config view mode
				m.configViewMode = false
				m.configScrollOffset = 0 // Reset scroll when exiting
				return m, nil
			case "ctrl+p":
				// Scroll up in config view
				if m.configScrollOffset > 0 {
					m.configScrollOffset--
				}
				return m, nil
			case "ctrl+n":
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
							targetIdx := m.traceData.GetEventIndexAtTime(targetTime)
							// Find nearest visible event from target (search forward first, then backward)
							found := false
							// Try forward
							for i := targetIdx; i < len(m.traceData.Events); i++ {
								if eventMatchesFilters(&m.traceData.Events[i], m.filterShowAll, m.filterList) {
									m.currentEventIndex = i
									m.currentTime = m.traceData.Events[i].TimeValue
									m.updateClusterState()
									found = true
									break
								}
							}
							// If not found forward, try backward
							if !found {
								for i := targetIdx - 1; i >= 0; i-- {
									if eventMatchesFilters(&m.traceData.Events[i], m.filterShowAll, m.filterList) {
										m.currentEventIndex = i
										m.currentTime = m.traceData.Events[i].TimeValue
										m.updateClusterState()
										break
									}
								}
							}
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

	// Handle search mode separately
	if m.searchMode {
		switch msg := msg.(type) {
		case tea.KeyMsg:
			switch msg.String() {
			case "enter":
				// Perform search with the entered pattern
				if searchText := m.searchInput.Value(); searchText != "" {
					m.searchPattern = searchText
					m.searchActive = true

					// Search for match
					var matchIndex int
					if m.searchDirection == "forward" {
						matchIndex = m.searchForward(m.currentEventIndex + 1, m.searchPattern)
					} else {
						matchIndex = m.searchBackward(m.currentEventIndex - 1, m.searchPattern)
					}

					if matchIndex >= 0 {
						m.currentEventIndex = matchIndex
						m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
						m.updateClusterState()
					}
				}
				// Exit search input mode but keep search active
				m.searchMode = false
				m.searchInput.Blur()
				return m, nil

			case "esc", "ctrl+c":
				// Cancel search input
				m.searchMode = false
				m.searchInput.Reset()
				m.searchInput.Blur()
				return m, nil
			}
		}

		// Update the search input
		m.searchInput, cmd = m.searchInput.Update(msg)
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

		case "h":
			// Enter help view mode
			m.helpViewMode = true
			return m, nil

		case "f":
			// Enter filter view mode
			m.filterViewMode = true
			m.filterInputActive = false
			m.filterSelectedIndex = -1
			m.filterInput.Blur() // Ensure input field is not focused
			return m, nil

		case "e":
			// Jump backward to previous MasterRecoveryState (any)
			if recovery := m.traceData.FindPreviousRecovery(m.currentEventIndex); recovery != nil {
				m.currentEventIndex = recovery.EventIndex
				m.currentTime = recovery.Time
				m.updateClusterState()
			}

		case "E", "shift+e":
			// Jump forward to next MasterRecoveryState (any)
			if recovery := m.traceData.FindNextRecovery(m.currentEventIndex); recovery != nil {
				m.currentEventIndex = recovery.EventIndex
				m.currentTime = recovery.Time
				m.updateClusterState()
			}

		case "ctrl+n":
			// Move forward to next visible (non-filtered) event
			if m.currentEventIndex < len(m.traceData.Events)-1 {
				// Find next event that matches filters
				for i := m.currentEventIndex + 1; i < len(m.traceData.Events); i++ {
					if eventMatchesFilters(&m.traceData.Events[i], m.filterShowAll, m.filterList) {
						m.currentEventIndex = i
						m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
						m.updateClusterState()
						break
					}
				}
			}

		case "ctrl+p":
			// Move backward to previous visible (non-filtered) event
			if m.currentEventIndex > 0 {
				// Find previous event that matches filters
				for i := m.currentEventIndex - 1; i >= 0; i-- {
					if eventMatchesFilters(&m.traceData.Events[i], m.filterShowAll, m.filterList) {
						m.currentEventIndex = i
						m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
						m.updateClusterState()
						break
					}
				}
			}

		case "ctrl+v":
			// Page forward (1 second) to next visible event
			newTime := m.currentTime + 1.0
			if newTime <= m.traceData.MaxTime {
				targetIdx := m.traceData.GetEventIndexAtTime(newTime)
				// Find next visible event from target
				for i := targetIdx; i < len(m.traceData.Events); i++ {
					if eventMatchesFilters(&m.traceData.Events[i], m.filterShowAll, m.filterList) {
						m.currentEventIndex = i
						m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
						m.updateClusterState()
						break
					}
				}
			}

		case "alt+v":
			// Page backward (1 second) to previous visible event
			newTime := m.currentTime - 1.0
			if newTime >= m.traceData.MinTime {
				targetIdx := m.traceData.GetEventIndexAtTime(newTime)
				// Find previous visible event from target
				for i := targetIdx; i >= 0; i-- {
					if eventMatchesFilters(&m.traceData.Events[i], m.filterShowAll, m.filterList) {
						m.currentEventIndex = i
						m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
						m.updateClusterState()
						break
					}
				}
			}

		case "g":
			// Jump to start - first visible event
			for i := 0; i < len(m.traceData.Events); i++ {
				if eventMatchesFilters(&m.traceData.Events[i], m.filterShowAll, m.filterList) {
					m.currentEventIndex = i
					m.currentTime = m.traceData.Events[i].TimeValue
					m.updateClusterState()
					break
				}
			}

		case "G", "shift+g":
			// Jump to end - last visible event
			for i := len(m.traceData.Events) - 1; i >= 0; i-- {
				if eventMatchesFilters(&m.traceData.Events[i], m.filterShowAll, m.filterList) {
					m.currentEventIndex = i
					m.currentTime = m.traceData.Events[i].TimeValue
					m.updateClusterState()
					break
				}
			}

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

		case "3":
			// Jump forward to next Severity=30 event
			for i := m.currentEventIndex + 1; i < len(m.traceData.Events); i++ {
				event := &m.traceData.Events[i]
				if event.Severity == "30" && eventMatchesFilters(event, m.filterShowAll, m.filterList) {
					m.currentEventIndex = i
					m.currentTime = event.TimeValue
					m.updateClusterState()
					break
				}
			}

		case "#", "shift+3":
			// Jump backward to previous Severity=30 event
			for i := m.currentEventIndex - 1; i >= 0; i-- {
				event := &m.traceData.Events[i]
				if event.Severity == "30" && eventMatchesFilters(event, m.filterShowAll, m.filterList) {
					m.currentEventIndex = i
					m.currentTime = event.TimeValue
					m.updateClusterState()
					break
				}
			}

		case "4":
			// Jump forward to next Severity=40 event
			for i := m.currentEventIndex + 1; i < len(m.traceData.Events); i++ {
				event := &m.traceData.Events[i]
				if event.Severity == "40" && eventMatchesFilters(event, m.filterShowAll, m.filterList) {
					m.currentEventIndex = i
					m.currentTime = event.TimeValue
					m.updateClusterState()
					break
				}
			}

		case "$", "shift+4":
			// Jump backward to previous Severity=40 event
			for i := m.currentEventIndex - 1; i >= 0; i-- {
				event := &m.traceData.Events[i]
				if event.Severity == "40" && eventMatchesFilters(event, m.filterShowAll, m.filterList) {
					m.currentEventIndex = i
					m.currentTime = event.TimeValue
					m.updateClusterState()
					break
				}
			}

		case "/":
			// Always enter forward search mode (start new search)
			m.searchMode = true
			m.searchDirection = "forward"
			m.searchInput.Focus()
			return m, textinput.Blink

		case "?":
			// Always enter backward search mode (start new search)
			m.searchMode = true
			m.searchDirection = "backward"
			m.searchInput.Focus()
			return m, textinput.Blink

		case "n":
			// Go to next match in the original search direction
			if m.searchActive && m.searchPattern != "" {
				var matchIndex int
				if m.searchDirection == "forward" {
					matchIndex = m.searchForward(m.currentEventIndex + 1, m.searchPattern)
				} else {
					matchIndex = m.searchBackward(m.currentEventIndex - 1, m.searchPattern)
				}
				if matchIndex >= 0 {
					m.currentEventIndex = matchIndex
					m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
					m.updateClusterState()
				}
			}

		case "N", "shift+n":
			// Go to previous match (opposite of original search direction)
			if m.searchActive && m.searchPattern != "" {
				var matchIndex int
				if m.searchDirection == "forward" {
					// Original was forward, so N goes backward
					matchIndex = m.searchBackward(m.currentEventIndex - 1, m.searchPattern)
				} else {
					// Original was backward, so N goes forward
					matchIndex = m.searchForward(m.currentEventIndex + 1, m.searchPattern)
				}
				if matchIndex >= 0 {
					m.currentEventIndex = matchIndex
					m.currentTime = m.traceData.Events[m.currentEventIndex].TimeValue
					m.updateClusterState()
				}
			}

		case "esc":
			// Clear search highlighting
			if m.searchActive {
				m.searchActive = false
				m.searchPattern = ""
				return m, nil
			}
		}

	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
	}

	return m, nil
}

// formatTraceEvent formats a trace event for display with config.fish field ordering and colors
func formatTraceEvent(event *TraceEvent, isCurrent bool, searchPattern string) string {
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
	searchHighlightStyle := lipgloss.NewStyle().Background(lipgloss.Color("58")) // Same as current line highlight

	var parts []string

	// Compile regex for search if pattern provided
	var searchRe *regexp.Regexp
	if searchPattern != "" {
		regexPattern := convertWildcardToRegex(searchPattern)
		searchRe, _ = regexp.Compile(regexPattern)
	}

	// Helper function to apply search highlighting to a field
	applySearchHighlight := func(text string) string {
		if searchRe == nil {
			return text
		}

		// Extract literal parts from the search pattern (between wildcards)
		literals := extractLiterals(searchPattern)
		if len(literals) == 0 {
			return text
		}

		// Collect all match positions for all literals
		type matchPos struct {
			start int
			end   int
		}
		var allMatches []matchPos

		for _, literal := range literals {
			if literal == "" {
				continue
			}
			// Create a regex for this literal (case-sensitive, escaped)
			literalPattern := regexp.QuoteMeta(literal)
			literalRe, err := regexp.Compile(literalPattern)
			if err != nil {
				continue
			}

			// Find all occurrences of this literal
			matches := literalRe.FindAllStringIndex(text, -1)
			for _, match := range matches {
				allMatches = append(allMatches, matchPos{start: match[0], end: match[1]})
			}
		}

		if len(allMatches) == 0 {
			return text
		}

		// Sort matches by start position
		sort.Slice(allMatches, func(i, j int) bool {
			return allMatches[i].start < allMatches[j].start
		})

		// Merge overlapping matches
		var merged []matchPos
		for _, match := range allMatches {
			if len(merged) == 0 {
				merged = append(merged, match)
			} else {
				last := &merged[len(merged)-1]
				if match.start <= last.end {
					// Overlapping or adjacent, merge them
					if match.end > last.end {
						last.end = match.end
					}
				} else {
					// Non-overlapping, add as new
					merged = append(merged, match)
				}
			}
		}

		// Build highlighted string
		var result strings.Builder
		lastEnd := 0
		for _, match := range merged {
			// Add text before match
			if match.start > lastEnd {
				result.WriteString(text[lastEnd:match.start])
			}
			// Add highlighted match
			result.WriteString(searchHighlightStyle.Render(text[match.start:match.end]))
			lastEnd = match.end
		}
		// Add remaining text
		if lastEnd < len(text) {
			result.WriteString(text[lastEnd:])
		}
		return result.String()
	}

	// Add fields in specific order: Time, Type, Severity (skip Machine, Roles, ID - shown in topology), then other attributes
	if event.Time != "" {
		parts = append(parts, fieldNameStyle.Render("Time=")+fieldValueStyle.Render(applySearchHighlight(event.Time)))
	}
	if event.Type != "" {
		parts = append(parts, fieldNameStyle.Render("Type=")+fieldValueStyle.Render(applySearchHighlight(event.Type)))
	}
	if event.Severity != "" {
		parts = append(parts, fieldNameStyle.Render("Severity=")+fieldValueStyle.Render(applySearchHighlight(event.Severity)))
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
		parts = append(parts, fieldNameStyle.Render(key+"=")+lipgloss.NewStyle().Foreground(lipgloss.Color("252")).Render(applySearchHighlight(value)))
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
func (m model) buildEventListPane(availableHeight int, paneWidth int, searchPattern string) []string {
	var lines []string
	currentIdx := m.currentEventIndex

	// We need to center based on LINE count, not event count
	// First, render the current event to see how many lines it takes
	currentEvent := &m.traceData.Events[currentIdx]
	currentEventLine := formatTraceEvent(currentEvent, false, searchPattern)
	currentWrappedLines := wrapText(currentEventLine, paneWidth)
	currentEventLineCount := len(currentWrappedLines)

	// Calculate how many lines we want above and below current event
	targetLinesAbove := availableHeight / 2

	// Build lines going backwards from current event
	var linesAbove []string
	lineCount := 0
	for i := currentIdx - 1; i >= 0 && lineCount < targetLinesAbove; i-- {
		event := &m.traceData.Events[i]

		// Skip filtered events
		if !eventMatchesFilters(event, m.filterShowAll, m.filterList) {
			continue
		}

		eventLine := formatTraceEvent(event, false, "") // No highlighting for non-current events
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

		// Skip filtered events
		if !eventMatchesFilters(event, m.filterShowAll, m.filterList) {
			continue
		}

		eventLine := formatTraceEvent(event, false, "") // No highlighting for non-current events
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
	// If in search mode, reserve 1 line for search bar
	eventListHeight := availableHeight
	if m.searchMode {
		eventListHeight = availableHeight - 1
	}

	// Pass search pattern if search is active
	searchPattern := ""
	if m.searchActive {
		searchPattern = m.searchPattern
	}
	eventLines := m.buildEventListPane(eventListHeight, rightPaneWidth, searchPattern)

	// If in search mode, add search bar as last line
	if m.searchMode {
		searchBarStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("252"))
		var searchBar string
		if m.searchDirection == "forward" {
			searchBar = "/" + m.searchInput.View()
		} else {
			searchBar = "?" + m.searchInput.View()
		}
		eventLines = append(eventLines, searchBarStyle.Render(searchBar))
	}

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
	help := helpStyle.Render("Ctrl+N/P: next/prev event | g/G: start/end | t: jump time | /?: search | n/N: next/prev match | f: filter | r/R: recovery | c: config | h: help | q: quit")
	bottomSection.WriteString(help)

	// Combine split view with bottom section
	fullView := splitContent.String() + bottomSection.String()

	// If in filter view mode, show filter popup overlay
	if m.filterViewMode {
		return m.renderFilterPopup(fullView)
	}

	// If in help view mode, show help popup overlay
	if m.helpViewMode {
		return m.renderHelpPopup(fullView)
	}

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

// renderFilterPopup renders the filter configuration popup overlay
func (m model) renderFilterPopup(baseView string) string {
	popupStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("39")).
		Padding(1, 2).
		Width(80)

	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("39")).
		Underline(true)

	sectionStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("46")).
		MarginTop(1)

	normalStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("252"))

	selectedStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("226")).
		Bold(true)

	var content strings.Builder
	content.WriteString(titleStyle.Render("Filter Configuration"))
	content.WriteString("\n\n")

	// Show "All" checkbox
	checkboxStyle := normalStyle
	checkbox := "[ ]"
	if m.filterShowAll {
		checkbox = "[x]"
	}
	content.WriteString(checkboxStyle.Render(fmt.Sprintf("%s All (space to toggle)", checkbox)))
	content.WriteString("\n")

	// INCLUSIVE filters section
	content.WriteString(sectionStyle.Render("INCLUSIVE Filters (OR):"))
	content.WriteString("\n")

	if len(m.filterList) == 0 {
		content.WriteString(normalStyle.Render("  (no filters)"))
		content.WriteString("\n")
	} else {
		for i, filter := range m.filterList {
			filterStyle := normalStyle
			prefix := "  "
			if i == m.filterSelectedIndex && !m.filterInputActive {
				filterStyle = selectedStyle
				prefix = "→ "
			}
			content.WriteString(filterStyle.Render(fmt.Sprintf("%s%s", prefix, filter)))
			content.WriteString("\n")
		}
	}

	content.WriteString("\n")

	// Input field for new filter
	if m.filterInputActive {
		content.WriteString(selectedStyle.Render("New filter: "))
		content.WriteString(m.filterInput.View())
	} else {
		content.WriteString(normalStyle.Render("Press 'i' to add new filter"))
	}

	content.WriteString("\n\n")
	content.WriteString(normalStyle.Render("Ctrl+N/P: navigate | Backspace: delete | Enter: add | Space: toggle All | q/f/Esc: close"))

	popup := popupStyle.Render(content.String())

	// Center the popup
	lines := strings.Split(baseView, "\n")
	popupLines := strings.Split(popup, "\n")

	// Calculate centering
	baseHeight := len(lines)
	popupHeight := len(popupLines)
	startLine := (baseHeight - popupHeight) / 2
	if startLine < 0 {
		startLine = 0
	}

	// Overlay the popup
	for i, popupLine := range popupLines {
		lineIdx := startLine + i
		if lineIdx < len(lines) {
			// Center horizontally
			baseWidth := m.width
			popupWidth := lipgloss.Width(popupLine)
			leftPadding := (baseWidth - popupWidth) / 2
			if leftPadding < 0 {
				leftPadding = 0
			}
			lines[lineIdx] = strings.Repeat(" ", leftPadding) + popupLine
		}
	}

	return strings.Join(lines, "\n")
}

// renderHelpPopup renders the help information popup overlay
func (m model) renderHelpPopup(baseView string) string {
	popupStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("39")).
		Padding(1, 2).
		Width(80)

	titleStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("39")).
		Underline(true)

	sectionStyle := lipgloss.NewStyle().
		Bold(true).
		Foreground(lipgloss.Color("46")).
		MarginTop(1)

	commandStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("252"))

	helpStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("241")).
		MarginTop(1)

	var content strings.Builder

	// ASCII Art Header
	asciiArt := `┌─────────────────┐
│  ╔═══════════╗  │
│  ║ FDB  TRIC ║  │
│  ║═══════════║  │
│  ║ ▓▓▓▓▓▓▓▓▓ ║  │
│  ║ ▓▓▓▓▓▓▓▓▓ ║  │
│  ║ ▓▓▓▓▓▓▓▓▓ ║  │
│  ╚═══════════╝  │
│  [■] [■] [■]    │
│  ○ ○ ○ ○ ○ ○    │
└─────────────────┘
   Scan the Timeline`

	content.WriteString(titleStyle.Render(asciiArt))
	content.WriteString("\n\n")

	// Navigation section
	content.WriteString(sectionStyle.Render("Navigation:"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  Ctrl+N / Ctrl+P    Next / previous trace event"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  Ctrl+V / Alt+V     Page forward / backward (±1 second)"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  g / G              Jump to start / end"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  t                  Jump to specific time"))
	content.WriteString("\n\n")

	// Search section
	content.WriteString(sectionStyle.Render("Search:"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  /                  Search forward (use * for wildcard)"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  ?                  Search backward (use * for wildcard)"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  n                  Go to next match"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  N                  Go to previous match"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  Esc                Clear search highlighting"))
	content.WriteString("\n\n")

	// Filter section
	content.WriteString(sectionStyle.Render("Filter:"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  f                  Configure event filters (toggle All, add/remove filters)"))
	content.WriteString("\n\n")

	// Recovery section
	content.WriteString(sectionStyle.Render("Recovery Navigation:"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  r / R              Jump to prev / next recovery start (StatusCode=0)"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  e / E              Jump to prev / next MasterRecoveryState (any)"))
	content.WriteString("\n\n")

	// Severity Navigation section
	content.WriteString(sectionStyle.Render("Severity Navigation:"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  3 / Shift+3        Jump to next / prev Severity=30 event"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  4 / Shift+4        Jump to next / prev Severity=40 event"))
	content.WriteString("\n\n")

	// View section
	content.WriteString(sectionStyle.Render("Views:"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  c                  Show full DB config JSON (Ctrl+N/P to scroll)"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  h                  Show this help"))
	content.WriteString("\n\n")

	// General section
	content.WriteString(sectionStyle.Render("General:"))
	content.WriteString("\n")
	content.WriteString(commandStyle.Render("  q / Q / Ctrl+C     Quit"))
	content.WriteString("\n")

	content.WriteString(helpStyle.Render("\nPress q/h/Esc to close"))

	popup := popupStyle.Render(content.String())

	// Overlay the popup on top of the base view
	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, popup, lipgloss.WithWhitespaceChars(" "))
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
		helpStyle.Render("Press q/c/Esc to close")

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
		helpStyle.Render("Press q/c/Esc to close | Ctrl+N/P to scroll")

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

// convertWildcardToRegex converts a simple wildcard pattern to regex
// * matches 0 or more characters
func convertWildcardToRegex(pattern string) string {
	// Escape regex special characters except *
	var result strings.Builder
	for _, ch := range pattern {
		switch ch {
		case '*':
			result.WriteString(".*")
		case '.', '+', '?', '^', '$', '(', ')', '[', ']', '{', '}', '|', '\\':
			result.WriteRune('\\')
			result.WriteRune(ch)
		default:
			result.WriteRune(ch)
		}
	}
	return result.String()
}

// extractLiterals extracts the non-wildcard literal parts from a search pattern
// For example: "*Recovery*State*" -> ["Recovery", "State"]
func extractLiterals(pattern string) []string {
	// Split by * to get literal parts
	parts := strings.Split(pattern, "*")
	var literals []string
	for _, part := range parts {
		if part != "" {
			literals = append(literals, part)
		}
	}
	return literals
}

// getEventFullText builds a full text representation of an event including ALL fields
func getEventFullText(event *TraceEvent) string {
	var parts []string

	// Include all standard fields
	if event.Time != "" {
		parts = append(parts, "Time="+event.Time)
	}
	if event.Type != "" {
		parts = append(parts, "Type="+event.Type)
	}
	if event.Severity != "" {
		parts = append(parts, "Severity="+event.Severity)
	}
	if event.Machine != "" {
		parts = append(parts, "Machine="+event.Machine)
	}
	if event.ID != "" {
		parts = append(parts, "ID="+event.ID)
	}

	// Include all attributes (sorted for consistency)
	var attrKeys []string
	for key := range event.Attrs {
		attrKeys = append(attrKeys, key)
	}
	sort.Strings(attrKeys)

	for _, key := range attrKeys {
		value := event.Attrs[key]
		parts = append(parts, key+"="+value)
	}

	return strings.Join(parts, " ")
}

// searchForward searches for pattern starting from startIndex going forward
// Returns the index of the first matching event, or -1 if not found
// Respects active filters - only searches visible (non-filtered) events
func (m *model) searchForward(startIndex int, pattern string) int {
	regexPattern := convertWildcardToRegex(pattern)
	re, err := regexp.Compile(regexPattern)
	if err != nil {
		return -1
	}

	for i := startIndex; i < len(m.traceData.Events); i++ {
		event := &m.traceData.Events[i]

		// Skip filtered events
		if !eventMatchesFilters(event, m.filterShowAll, m.filterList) {
			continue
		}

		eventText := getEventFullText(event)
		if re.MatchString(eventText) {
			return i
		}
	}

	// Wrap around to beginning
	for i := 0; i < startIndex; i++ {
		event := &m.traceData.Events[i]

		// Skip filtered events
		if !eventMatchesFilters(event, m.filterShowAll, m.filterList) {
			continue
		}

		eventText := getEventFullText(event)
		if re.MatchString(eventText) {
			return i
		}
	}

	return -1
}

// searchBackward searches for pattern starting from startIndex going backward
// Returns the index of the first matching event, or -1 if not found
// Respects active filters - only searches visible (non-filtered) events
func (m *model) searchBackward(startIndex int, pattern string) int {
	regexPattern := convertWildcardToRegex(pattern)
	re, err := regexp.Compile(regexPattern)
	if err != nil {
		return -1
	}

	for i := startIndex; i >= 0; i-- {
		event := &m.traceData.Events[i]

		// Skip filtered events
		if !eventMatchesFilters(event, m.filterShowAll, m.filterList) {
			continue
		}

		eventText := getEventFullText(event)
		if re.MatchString(eventText) {
			return i
		}
	}

	// Wrap around to end
	for i := len(m.traceData.Events) - 1; i > startIndex; i-- {
		event := &m.traceData.Events[i]

		// Skip filtered events
		if !eventMatchesFilters(event, m.filterShowAll, m.filterList) {
			continue
		}

		eventText := getEventFullText(event)
		if re.MatchString(eventText) {
			return i
		}
	}

	return -1
}

// eventMatchesFilters checks if an event matches any of the filter patterns (OR logic)
// Returns true if:
// - showAll is true, OR
// - filterList is empty, OR
// - event matches at least one filter pattern
func eventMatchesFilters(event *TraceEvent, showAll bool, filterList []string) bool {
	// If "All" is checked or no filters, show everything
	if showAll || len(filterList) == 0 {
		return true
	}

	// Get full event text for matching
	eventText := getEventFullText(event)

	// Check if event matches any filter (OR logic)
	for _, filter := range filterList {
		regexPattern := convertWildcardToRegex(filter)
		re, err := regexp.Compile(regexPattern)
		if err != nil {
			continue // Skip invalid patterns
		}
		if re.MatchString(eventText) {
			return true
		}
	}

	return false
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
