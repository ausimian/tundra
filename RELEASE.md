### Added

- `Tundra.adopt/1` to take ownership of an already-created TUN device from an
  open file descriptor. The original descriptor is duplicated and closed, so it
  must not be used after a successful call.

### Changed

- **Breaking**: Raise the minimum required Elixir version to 1.18 (was 1.15).
