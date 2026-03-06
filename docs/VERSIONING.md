# Versioning Policy

## Format
- Version format: `X.Y.Z`

## Meaning
- `X` (major): functionality architecture changes and large refactors.
- `Y` (minor): bug fixes and incremental functionality additions.
- `Z` (patch/sub-version): every successful build and flash cycle.

## Operational Rule
- After each successful `idf.py build flash`, confirm whether to:
  1. commit code changes
  2. create a sub-version (`Z = Z + 1`)

## Current Version Source
- Canonical version is stored in repository root file: `VERSION`
