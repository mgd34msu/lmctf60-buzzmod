# SLIPGATE C style

## Modules

- Keep one subsystem in each translation unit.
- Keep private state and helpers `static`.
- Put only cross-module declarations in headers.
- Split a file when unrelated state or lifecycle rules appear in it.

`tools/source-size-budget.json` defines the authored-source policy. Eight hundred
lines is a review threshold, not a hard module limit. A cohesive module may
exceed it when another split would divide one subsystem. An authored file may
contain at most 9,999 lines. Files without a reviewed line-length exception may
not contain lines over 100 columns after tab expansion.

Do not grow an existing monolith merely because it already exceeds the review
threshold. Split at a subsystem, state owner, or lifecycle boundary. Do not
split a file into numbered fragments or manufacture tiny files to satisfy a
line count.

## Functions and state

- Give each function one job with a name that states the decision it makes.
- Pass shared per-frame state through a context struct such as `sg_think_t`.
- Define cvars and defaults only in `sg_cvars.h`.
- Extract a helper when production logic repeats.
- Keep mutable state owned by one module and one lifecycle.

## Runtime boundaries

- Debug gates may emit diagnostics. They may not assign production state.
- Derive paths from the game directory and configuration. Do not embed local
  checkout paths, ports, or operator names.
- Admit enemy and objective knowledge only through the perception and public
  game-state interfaces.
- Keep navigation, combat, and presentation authority in their owning modules.

## Comments

Comments may state an external protocol rule, a non-obvious invariant, units,
or why a tempting implementation is wrong. Keep them short. Do not record trial
results, implementation history, reviewer conversations, dates, ownership
rulings, or the code's control flow. Git history holds that material.

The build rejects oversized authored files and unbudgeted lines over 100
columns. Tests must anchor to functions or executable statements, never prose.

## Changes and verification

- Keep mechanical cleanup separate from behavior changes.
- Build the complete module and read raw compiler output.
- Test changed policy through executable state, not source comments.
- Run both Make dialects before merging a source milestone.
- Verify each CI job rather than trusting an aggregate label.

## Naming

- Use a module prefix for private function families.
- Use `SG_` for cross-module interfaces.
- Name a function after the decision or action it owns.
- Include units in names or adjacent compact comments when the type cannot.

Player and administrator output uses game language. Development terms belong in
debug output and development tools, not menus, broadcasts, commands, or release
notes.
