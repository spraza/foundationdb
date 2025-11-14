# FDB Scrubber - Design Documentation

## Project Name: FDB Scrubber

### Naming Discussion

**Why "Scrubber"**:
- **Descriptive**: Scrubbing through trace timelines to find events
- **Short & Memorable**: Easy to type as a binary name
- **Intuitive**: "Scrubbing" is a well-known term for navigating through timelines (like video scrubbing)

**Tagline Options**:
- "Scan the Timeline"
- "Scan • Navigate • Analyze"
- "The FDB Trace Timeline Scanner"

---

## ASCII Art Designs

### Version 1: Compact (Used in Help Screen - Press 'h')

```
┌─────────────────┐
│  ╔═══════════╗  │
│  ║ FDB SCRUB ║  │
│  ║═══════════║  │
│  ║ ▓▓▓▓▓▓▓▓▓ ║  │
│  ║ ▓▓▓▓▓▓▓▓▓ ║  │
│  ║ ▓▓▓▓▓▓▓▓▓ ║  │
│  ╚═══════════╝  │
│  [■] [■] [■]    │
│  ○ ○ ○ ○ ○ ○    │
└─────────────────┘
   Scan the Timeline
```

**Usage**: Displayed at the top of the help popup (press 'h' in the application)

---

### Version 2: Detailed (For README/Marketing)

```
        ╔════════════════════════╗
        ║                        ║
        ║    ╭──────────────╮    ║
        ║    │ FDB SCRUBBER │    ║
        ║    ╰──────────────╯    ║
        ║  ┌──────────────────┐  ║
        ║  │ ████████████████ │  ║
        ║  │ ██ Trace Events █ │  ║
        ║  │ ████████████████ │  ║
        ║  │ Time: 4.333268s  │  ║
        ║  │ Events: 150,432  │  ║
        ║  │ █▓▒░█▓▒░█▓▒░█▓▒░ │  ║
        ║  └──────────────────┘  ║
        ║                        ║
        ║   [SCAN] [NAV] [FILT]  ║
        ║    ○  ○  ○  ○  ○  ○    ║
        ╚════════════════════════╝
             ║ ║ ║ ║ ║
             ▼ ▼ ▼ ▼ ▼
        Scanning Timeline...
```

**Usage**: README header, documentation, marketing materials

---

### Version 3: Banner Style (For start-up splash screen)

```
    ___________  ____     _____ __________  __  ______
   / ____/ __ \/ __ )   / ___// ____/ __ \/ / / / __ )
  / /_  / / / / __  |   \__ \/ /   / /_/ / / / / __  |
 / __/ / /_/ / /_/ /   ___/ / /___/ _, _/ /_/ / /_/ /
/_/   /_____/_____/   /____/\____/_/ |_|\____/_____/

    ╔══════════════════════════════════╗
    ║   Scan • Navigate • Analyze      ║
    ║   The FDB Trace Timeline         ║
    ╚══════════════════════════════════╝
```

**Usage**: start-up splash screen, banner for blog posts/articles

---

### Version 4: Animated/Active State

```
┏━━━━━━━━━━━━━━━━━━━━━┓
┃ ╔═══════════════╗   ┃
┃ ║ FDB SCRUB  ◉  ║   ┃ ← Scanning indicator
┃ ╠═══════════════╣   ┃
┃ ║ ▓▓▓▓▓▓░░░░░░░ ║   ┃
┃ ║ Recovery: 14   ║   ┃
┃ ║ Severity: 30   ║   ┃
┃ ║ Coord: Active  ║   ┃
┃ ║ ▓▓░░░░░░░░░░░ ║   ┃
┃ ╚═══════════════╝   ┃
┃  ⏮  ⏪  ⏸  ⏩  ⏭    ┃
┃  [●][●][●][●][●]    ┃
┗━━━━━━━━━━━━━━━━━━━━━┛
```

**Usage**: Active scanning state visualization, demo screenshots

---

## Design Philosophy

### Visual Elements
- **Clean & Functional**: Engineers appreciate clean, purposeful design
- **Box Drawing Characters**: Uses Unicode box-drawing characters for crisp terminal rendering
- **Progress Indicators**: Block characters (▓▒░) show data flow and scanning states

### Color Concepts (for future terminal color support)
- **Blue**: Primary interface elements
- **Green**: Active scanning/data streams
- **Amber/Yellow**: Warnings and alerts
- **Red**: Critical events (Severity=40)

### Metaphors
- **Scanning Device**: You're scanning through a timeline
- **Time Navigation**: Navigating through temporal data, not just scrolling logs
- **Data Analysis**: Analysis of complex distributed system states

---

## Marketing Angles

### For Engineers
- "Stop grep-ing trace logs. Start scanning timelines."

### Technical Benefits
- Interactive time-based navigation
- Filter and search capabilities
- Visual cluster topology
- Recovery state tracking
- Severity-based event jumping

---

## Future Enhancements

### Possible Features
- Color themes
- Export filtered event sets
- Bookmark/annotation system
- Multi-trace comparison mode
- Plugin architecture for custom event parsers

### ASCII Art Variations to Consider
- Different orientations
- "Scanning..." animation frames
- Status indicators for different tool states
- Mini icon for status bar

---

## Credits

FDB trace timeline analysis and debugging tool.
