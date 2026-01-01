# CHANGELOG

## 0.3.1 - 2026-01-01

- Add Linux ARM64 server package build

## 0.3.0 - 2026-01-01

- Restrict socket access to tundra group members for improved security
- Add tundra group creation to deb/pkg postinstall scripts

## 0.2.1 - 2025-12-31

- Fix release workflow to correctly publish deb packages
- Include architecture in macOS package filename

## 0.2.0 - 2025-12-31

- Extract server into standalone C component at `c_src/server/`
- Add direct TUN device creation for privileged operation (Linux and macOS)
- Add deb and pkg packaging with systemd/launchd service integration
- Add server package builds and GitHub releases to CI

## 0.1.9 - 2025-01-12

- Internal CI/CD changes only

## 0.1.3 - 2025-01-12

- Typos
- mix format

## 0.1.2 - 2025-01-12

- Fix up some hex package stuff

## 0.1.1 - 2025-01-12

- Fix source_ref in docs generation

## 0.1.0 - 2025-01-12

Initial revision
