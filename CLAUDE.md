@AGENTS.md

## Claude Code

The shared rules for all agents are in `AGENTS.md` (imported above). Put new project rules there, not here, so every tool gets them. This section is only for Claude-specific habits.

- **One issue per session.** Start a new session (or `/clear`) for each task, and don't carry a finished task's context into the next one.
- **Plan first for risky areas.** Use plan mode for anything touching flash layout, calibration, saved settings (`tomline/ui.cc`) or the bootloader, and wait for approval.
- **Keep the context small.** Stay in `tomline/`. Read stock code (`streams/`, `stmlib/`) only for the part Tomline uses. Send broad "where is X used?" searches to the Explore subagent.
- **Show proof.** Before saying something is done, run the render test and report its numbers, and say whether the firmware built.
- **Learn once.** If the owner corrects you on something every agent should know, propose a one-line addition to `AGENTS.md`.
- Personal, uncommitted preferences go in `CLAUDE.local.md` (gitignored).
