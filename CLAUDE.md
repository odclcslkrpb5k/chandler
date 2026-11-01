Read docs/claude-notes.md at the start of every session (it is gitignored: Claude's own
durable context). The plan is docs/plan.md; the brief is docs/CHANDLER_HANDOFF.md.

## Standard

This project follows the Paranoid TypeScript standard's C/GTK 3 appendix:
https://github.com/odclcslkrpb5k/paranoid-typescript/blob/fecdf9cb06cadf76a3d65734cab47a96ad85da68/appendices/c-gtk.md
Last conformed to commit: fecdf9cb06cadf76a3d65734cab47a96ad85da68

Every fix, control and action item names its rung on the Hierarchy of Defect Controls
(Eliminate, Substitute, Engineer, Administer, Protect).

## Deviations (RP-6)

- GTK 3, not GTK 4 (the appendix says a new project weighs GTK 4 first): matches Quoin, the
  appendix's settings were proven on GTK 3, and the owner chose it (plan §8).
- No CI and no git hooks (SH-11's commit-time gate): the owner's rule, as for Quoin. The gate
  is `./build release`, which refuses a dirty tree and stamps the git hash.
- LN-5 (a condition is a boolean) has no C tool; a known gap of the appendix.
- RP-3's notes file is gitignored (owner, 2026-11-01): durable context that matters to others
  goes into docs/plan.md or commit messages instead.
- No fuzzing until the first parser exists (milestone 2); `./build check` gains `fuzz` then,
  with an assertion that it found its targets (TS-6).

## Commands

`./build check` runs every check and ends with a PASS or FAIL line. `./build format` rewrites
the C sources. Everything runs in the Docker image; see README.md.
