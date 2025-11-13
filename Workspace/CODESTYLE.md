# Code Style

This project uses a mixed English convention to balance clarity in code with local preferences in documentation.

- Identifiers (constants, variables, functions, types): US English
  - Examples: `color`, `center`, `normalize`, `behavior`, `gray`
  - Do not introduce UK spellings in identifiers.
- Comments and docs: British English
  - Examples in comments only: colour, centre, normalise, behaviour, grey

Additional notes

- External APIs and libraries must not be renamed. For example, LovyanGFX uses colour constants like `TFT_DARKGREY`; keep these as-is when referencing them.
- Abbreviations are allowed where established and unambiguous (e.g., `col` for colour/color in temporary local variables, `rad` for radians).
- Prefer descriptive names over terse ones for public functions and shared utilities.
- Keep names consistent across files. If a US/UK spelling conflict is discovered in identifiers, standardise to US English throughout the project in one change.
- Use PascalCase for types/classes, camelCase for functions and variables, and ALL_CAPS for compile-time constants and macros unless constrained by an external API.
