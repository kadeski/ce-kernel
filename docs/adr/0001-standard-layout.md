# 0001. Standard project layout

Status: accepted
Date: 2026-10-24

## Context
The author's projects share a standard layout so each one is quick to come back to: pure
logic in `core/`, I/O in `adapters/`, interchangeable units in a registry folder, helper
scripts in `scripts/`, and `docs/DESIGN.md`, `docs/adr/` and `docs/plans/` for documentation.
The shared template is a Python package (uv, `pyproject.toml`). ce-kernel is C for the eZ80,
built by the CE toolchain's makefile, so its folders can't be copied one for one.

## Decision
Keep the template's layers and documentation, mapped onto C:

| Template | ce-kernel |
|---|---|
| `core/`: pure logic, no I/O | `src/core/`: checked 64-bit math, exact fractions, the expression evaluator |
| `adapters/`: all I/O | `src/drivers/`: the hardware registers |
| `plugins/`: one file per unit, found by name | `src/discrete/`: one section of tools per file, each a `tools[]` table; `home[]` in `src/kernel/menu.c` lists the sections |
| `cli.py`: entry point | the loaders, `src/tios/` and `rom/`, which call `kmain()` in `src/kernel/` |
| `models.py`: shared types | `src/kernel/kernel.h` (`struct boot_info`) |
| `pyproject.toml`: build and tool config | `makefile` |
| `scripts/` | `scripts/` |
| `data/`, `build/`: generated files | `build/` (objects for each build) and `bin/` (the programs) |

`rom/` stays outside `src/`: the TI-OS build uses the toolchain's stock makefile, which
compiles every file under `src/`, and the bare ROM's reset code must not end up in
`KERNEL.8xp`. For the same reason `src/tios/` is filtered out of the ROM build.

## Consequences
The docs are in the same places as in the other projects, and the layer rules can be stated
by folder: `src/core/` includes nothing outside itself, and `src/drivers/` includes only
`src/drivers/`. `src/kernel/` still mixes the screens, the shell and the kernel's services;
`docs/DESIGN.md` says where it splits next and what is in the way.
