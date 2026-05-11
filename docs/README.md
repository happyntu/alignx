# Documentation

This directory is the project documentation hub.

## Layout

```text
docs/
  architecture.md   Module map and data flow
  roadmap.md        Phase plan and milestones
  adr/              Accepted architectural decisions
  design/           Design notes and feature sketches
  dev/              Developer workflow notes
  research/         Exploratory research and comparison notes
```

## Source of Truth

When documents disagree, use the order defined in `CLAUDE.md`:

1. `docs/adr/`
2. `docs/architecture.md`
3. `docs/roadmap.md`
4. implementation in `src/` and `tests/`
5. exploratory notes in `docs/research/`

New decisions belong in ADRs. Exploratory notes belong in `docs/research/`.
