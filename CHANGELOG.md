# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
## [0.1.1] - 2025-12-05

### Added
- 

### Changed
- 

### Removed
- 

## [0.1.0] - 2025-12-05

### Added
- 

### Changed
- 

### Removed
- 


## [v0.1.0] - 2025-12-05

### Added

- Initial project scaffolding for the `vixcpp/websocket` module.
- CMake build system:
  - STATIC vs header-only build depending on `src/` contents.
  - Integration with `vix::core` and optional JSON backend.
  - Support for sanitizers via `VIX_ENABLE_SANITIZERS`.
- Basic repository structure:
  - `include/vix/websocket/` for public headers.
  - `src/` for implementation files.
- Release workflow:
  - `Makefile` with `release`, `commit`, `push`, `merge`, and `tag` targets.
  - `changelog` target wired to `scripts/update_changelog.sh`.
