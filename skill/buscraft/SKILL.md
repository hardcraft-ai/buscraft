---
name: buscraft
description: Control lab instruments over GPIB through a BusCraft adapter - find the adapter, query an instrument, take readings and log them. Use when the user wants to talk to a GPIB / IEEE-488 instrument.
---

# BusCraft

Status: written alongside the first release.

## Writing an instrument skill

When an instrument has no skill yet, identify it, read its manual, try the commands on the instrument and write a new
skill from what worked. Follow [CONTRIBUTING.md](../../CONTRIBUTING.md):

- Write only your own work. Never copy text, tables, figures, code or definition files from manuals, datasheets,
  vendor drivers, closed tools or other projects into the skill or the repository, and never add the source files.
- Facts such as command syntax, ranges and timings may be used: restate them in your own words and structure.
- List the sources as references (title, document number, link), and the hardware and firmware the skill was verified
  on. Mark it as an AI draft until it has been verified on the instrument.
