package main

import (
	"encoding/json"
	"fmt"
	"strconv"
	"strings"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

// model holds the UI state for the Bubbletea application
type model struct {
	traceData      *TraceData
	currentTime    float64
	clusterState   *ClusterState
	width          int
	height         int
	timeInputMode  bool
	timeInput      textinput.Model
	configViewMode bool
	scrollOffset   int // Vertical scroll offset for topology pane
}

// newModel creates a new model with the given trace data
func newModel(traceData *TraceData) model {
	ti := textinput.New()
	ti.Placeholder = "Enter time in seconds (e.g., 123.456)"
	ti.CharLimit = 20
	ti.Width = 40

	return model{
		traceData:      traceData,
		currentTime:    0.0,
		clusterState:   NewClusterState(),
		timeInputMode:  false,
		timeInput:      ti,
		configViewMode: false,
		scrollOffset:   0,
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
							m.currentTime = targetTime
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
			// Move forward in time
			newTime := m.currentTime + m.traceData.TimeStep
			if newTime <= m.traceData.MaxTime {
				m.currentTime = newTime
				m.updateClusterState()
			}

		case "ctrl+p":
			// Move backward in time
			newTime := m.currentTime - m.traceData.TimeStep
			if newTime >= m.traceData.MinTime {
				m.currentTime = newTime
				m.updateClusterState()
			}

		case "right":
			// Fast forward (1 second)
			newTime := m.currentTime + 1.0
			if newTime <= m.traceData.MaxTime {
				m.currentTime = newTime
				m.updateClusterState()
			}

		case "left":
			// Fast backward (1 second)
			newTime := m.currentTime - 1.0
			if newTime >= m.traceData.MinTime {
				m.currentTime = newTime
				m.updateClusterState()
			}

		case "g":
			// Jump to start (like less)
			m.currentTime = m.traceData.MinTime
			m.updateClusterState()

		case "G", "shift+g":
			// Jump to end (like less)
			m.currentTime = m.traceData.MaxTime
			m.updateClusterState()

		case "r":
			// Jump backward to latest MasterRecoveryState with StatusCode="0"
			if recovery := m.traceData.FindPreviousRecoveryWithStatusCode(m.currentTime, "0"); recovery != nil {
				m.currentTime = recovery.Time
				m.updateClusterState()
			}

		case "R", "shift+r":
			// Jump forward to earliest MasterRecoveryState with StatusCode="0"
			if recovery := m.traceData.FindNextRecoveryWithStatusCode(m.currentTime, "0"); recovery != nil {
				m.currentTime = recovery.Time
				m.updateClusterState()
			}

		case "up":
			// Scroll up in topology pane
			if m.scrollOffset > 0 {
				m.scrollOffset--
			}

		case "down":
			// Scroll down in topology pane
			m.scrollOffset++
			// Clamp to reasonable max (will be display-clamped in View)
			// We use height as rough estimate - actual max is computed in View
			if m.scrollOffset > m.height*10 {
				m.scrollOffset = m.height * 10
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
	// Skip fields as per config.fish
	skipFields := map[string]bool{
		"DateTime":         true,
		"ThreadID":         true,
		"LogGroup":         true,
		"TrackLatestType":  true,
	}

	// Color styles
	fieldNameStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("240")) // Dim
	fieldValueStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("46"))  // Green
	currentLineStyle := lipgloss.NewStyle().Background(lipgloss.Color("237")) // Highlight background

	var parts []string

	// Add fields in specific order: Time, Type, Severity, Machine, Roles
	if event.Time != "" {
		parts = append(parts, fieldNameStyle.Render("Time=") + fieldValueStyle.Render(event.Time))
	}
	if event.Type != "" {
		parts = append(parts, fieldNameStyle.Render("Type=") + fieldValueStyle.Render(event.Type))
	}
	if event.Severity != "" {
		parts = append(parts, fieldNameStyle.Render("Severity=") + fieldValueStyle.Render(event.Severity))
	}
	if event.Machine != "" {
		parts = append(parts, fieldNameStyle.Render("Machine=") + fieldValueStyle.Render(event.Machine))
	}
	if roles, ok := event.Attrs["Roles"]; ok && !skipFields["Roles"] {
		parts = append(parts, fieldNameStyle.Render("Roles=") + fieldValueStyle.Render(roles))
	}

	// Add remaining attributes
	for key, value := range event.Attrs {
		if key == "Roles" {
			continue // Already handled
		}
		if skipFields[key] {
			continue
		}
		parts = append(parts, fieldNameStyle.Render(key+"=") + lipgloss.NewStyle().Foreground(lipgloss.Color("252")).Render(value))
	}

	line := strings.Join(parts, " ")
	if isCurrent {
		return currentLineStyle.Render(line)
	}
	return line
}


func (m *model) updateClusterState() {
	events := m.traceData.GetEventsUpToTime(m.currentTime)
	m.clusterState = BuildClusterState(events)
	// Reset scroll to top when time changes
	m.scrollOffset = 0
}

// View renders the UI (required by Bubbletea)
func (m model) View() string {

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

	roleStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("252")) // Normal gray color

	scrubberStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("241")).
		BorderStyle(lipgloss.NormalBorder()).
		BorderTop(true).
		PaddingTop(1).
		PaddingLeft(1)

	helpStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("241"))

	// Build the view
	var topologyLines []string

	// Get workers grouped by DC and testers
	dcWorkers := m.clusterState.GetWorkersByDC()
	testers := m.clusterState.GetTesters()

	if len(dcWorkers) == 0 && len(testers) == 0 {
		topologyLines = append(topologyLines, "")
		topologyLines = append(topologyLines, "  <NO_CLUSTER_YET>")
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
				topologyLines = append(topologyLines, dcHeaderStyle.Render(fmt.Sprintf("DC %s", dcID)))

				for _, worker := range workers {
					// Display worker
					if worker.HasRoles() {
						// Green dot for workers with roles
						workerLine := fmt.Sprintf("● %s", worker.Machine)
						topologyLines = append(topologyLines, workerStyleGreen.Render(workerLine))

						// Show each role on a separate indented line
						for _, role := range worker.Roles {
							topologyLines = append(topologyLines, roleStyle.Render("      "+role))
						}
					} else {
						// Gray dot for workers without roles
						workerLine := fmt.Sprintf("● %s", worker.Machine)
						topologyLines = append(topologyLines, workerStyleGray.Render(workerLine))
					}
				}
			}
		}

		// Display testers in a separate section
		if len(testers) > 0 {
			topologyLines = append(topologyLines, testerHeaderStyle.Render("Testers"))

			for _, worker := range testers {
				// Display tester
				if worker.HasRoles() {
					// Green dot for testers with roles
					workerLine := fmt.Sprintf("● %s", worker.Machine)
					topologyLines = append(topologyLines, workerStyleGreen.Render(workerLine))

					// Show each role on a separate indented line
					for _, role := range worker.Roles {
						topologyLines = append(topologyLines, roleStyle.Render("      "+role))
					}
				} else {
					// Gray dot for testers without roles
					workerLine := fmt.Sprintf("● %s", worker.Machine)
					topologyLines = append(topologyLines, workerStyleGray.Render(workerLine))
				}
			}
		}
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

	// Apply scroll offset
	totalLines := len(topologyLines)
	visibleLines := topologyLines

	// Calculate max scroll
	maxScrollOffset := totalLines - availableHeight
	if maxScrollOffset < 0 {
		maxScrollOffset = 0
	}

	// Clamp the display scroll offset (doesn't modify model, just for display)
	displayScrollOffset := m.scrollOffset
	if displayScrollOffset < 0 {
		displayScrollOffset = 0
	}
	if displayScrollOffset > maxScrollOffset {
		displayScrollOffset = maxScrollOffset
	}

	// Apply clamped scroll offset
	if displayScrollOffset > 0 && displayScrollOffset < totalLines {
		visibleLines = topologyLines[displayScrollOffset:]
	} else if displayScrollOffset >= totalLines {
		visibleLines = []string{}
	} else {
		// displayScrollOffset == 0, show all from start
		visibleLines = topologyLines
	}

	// Determine if we'll show "more below" indicator
	hasMoreBelow := displayScrollOffset < maxScrollOffset && len(visibleLines) > availableHeight-1

	// Reserve 1 line for scroll indicator if needed
	maxVisibleLines := availableHeight
	if hasMoreBelow {
		maxVisibleLines = availableHeight - 1
	}

	// Limit visible lines to available space
	if len(visibleLines) > maxVisibleLines {
		visibleLines = visibleLines[:maxVisibleLines]
	}

	// Build final content
	var content strings.Builder

	for _, line := range visibleLines {
		content.WriteString(line)
		content.WriteString("\n")
	}

	// Add scroll indicator if there's more content below
	if hasMoreBelow {
		scrollIndicator := lipgloss.NewStyle().
			Foreground(lipgloss.Color("240")).
			Render("... (more below, press Down to scroll)")
		content.WriteString(scrollIndicator)
		content.WriteString("\n")
	}

	// Add remaining blank lines to push bottom section down
	usedLines := len(visibleLines)
	if hasMoreBelow {
		usedLines++ // Account for scroll indicator
	}
	remainingLines := availableHeight - usedLines
	if remainingLines > 0 {
		content.WriteString(strings.Repeat("\n", remainingLines))
	}

	// DB Configuration section
	config := m.traceData.GetLatestConfigAtTime(m.currentTime)
	if config != nil {
		configStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("243")).
			BorderStyle(lipgloss.NormalBorder()).
			BorderTop(true).
			PaddingTop(1).
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
		if config.StorageEngine != "" {
			configParts = append(configParts, fmt.Sprintf("storage_engine=%s", config.StorageEngine))
		}
		if config.LogEngine != "" {
			configParts = append(configParts, fmt.Sprintf("log_engine=%s", config.LogEngine))
		}

		configContent += configValueStyle.Render(strings.Join(configParts, " | "))
		content.WriteString(configStyle.Render(configContent))
		content.WriteString("\n")
	}

	// Recovery State section
	recoveryState := m.traceData.GetLatestRecoveryStateAtTime(m.currentTime)
	if recoveryState != nil {
		recoveryStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("243")).
			PaddingLeft(1)

		recoveryTitleStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("39")).
			Bold(true)

		recoveryValueStyle := lipgloss.NewStyle().
			Foreground(lipgloss.Color("252"))

		recoveryContent := recoveryTitleStyle.Render(fmt.Sprintf("Recovery State (t=%.6fs)", recoveryState.Time)) + " "
		recoveryContent += recoveryValueStyle.Render(fmt.Sprintf("StatusCode=%s | Status=%s", recoveryState.StatusCode, recoveryState.Status))

		content.WriteString(recoveryStyle.Render(recoveryContent))
		content.WriteString("\n")
	}

	// Time scrubber
	scrubberContent := fmt.Sprintf("Time: %.6fs", m.currentTime)
	content.WriteString(scrubberStyle.Render(scrubberContent))
	content.WriteString("\n")

	// Help text
	help := helpStyle.Render("Up/Down: scroll | Ctrl+N/P: step | Left/Right: fast scrub | g/G: start/end | t: jump | r/R: recovery | c: config | q: quit")
	content.WriteString(help)

	baseView := content.String()

	// If in config view mode, show config popup overlay
	if m.configViewMode {
		config := m.traceData.GetLatestConfigAtTime(m.currentTime)
		if config != nil {
			return m.renderConfigPopup(baseView, config)
		}
	}

	// If in time input mode, show popup overlay
	if m.timeInputMode {
		return m.renderTimeInputPopup(baseView)
	}

	return baseView
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
		Foreground(lipgloss.Color("252")).
		MarginTop(1)

	// Pretty-print the JSON
	jsonBytes, err := json.MarshalIndent(config.RawJSON, "", "  ")
	var jsonContent string
	if err != nil {
		jsonContent = "Error formatting JSON"
	} else {
		jsonContent = string(jsonBytes)
	}

	// Build popup content
	popupContent := titleStyle.Render(fmt.Sprintf("DB Config (t=%.2fs)", config.Time)) + "\n" +
		jsonStyle.Render(jsonContent) + "\n" +
		helpStyle.Render("Press q or c to close")

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
