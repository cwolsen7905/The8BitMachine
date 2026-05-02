# Contributing to The 8-Bit Machine

Thanks for your interest in contributing. The project is a general-purpose 8-bit machine designer built in C++17 with Dear ImGui. Contributions are welcome — new chip implementations, CPU cores, preset machines, bug fixes, and tooling improvements all fit.

---

## Before you start

- Open an issue to discuss anything beyond a trivial fix. This avoids duplicate work and lets us agree on scope before you invest time.
- One logical change per pull request. A new device and a preset that uses it can share a PR; an unrelated style pass cannot.
- The project targets macOS. Cross-platform patches are welcome but will not block or delay merges for contributors on other platforms.

---

## Setting up

```bash
# Install prerequisites (macOS)
brew install cmake sdl2 cc65

# Clone and build
git clone <repo-url>
cd the-8-bit-machine
./build.sh
```

cc65 (ca65/ld65) is only needed if you modify the test ROMs in `roms/`. The build script warns and skips assembly if cc65 is absent.

See [README.md](README.md) for full prerequisites and build options.

---

## Workflow

1. Fork the repository and create a branch off `dev` (not `main`).
2. Make your changes. Keep commits focused — one logical change per commit.
3. Build and run the application. Exercise the feature manually.
4. Open a pull request against `dev` with a short description of what changed and why.

`main` is the release branch; `dev` is where active development happens.

---

## Code style

- C++17, no exceptions in hot paths.
- Indentation: 4 spaces. No tabs.
- Follow the naming conventions you see around you: `camelCase` for methods and locals, `PascalCase` for types and classes, `UPPER_SNAKE` for compile-time constants and register bit masks.
- Keep headers minimal — forward-declare where possible, include only what you need.
- No trailing whitespace.
- No co-author or tool-generated attribution in commit messages.

---

## Adding a device

Read [docs/contributor-guide.md](docs/contributor-guide.md) — it covers the full `IBusDevice` / `IHasPanel` / `IPeripheral` / `IIECDevice` interface contracts, what Machine guarantees to devices, and how to wire a new device into a preset.

---

## Adding a preset machine

Read the **Preset authoring** section in [docs/contributor-guide.md](docs/contributor-guide.md). New presets require a JSON file in `presets/` and a `buildXxxPreset()` method in `Machine`, plus a one-line entry in the `kPresetDrivers[]` table in `Application.cpp`.

---

## Reporting bugs

Open a GitHub issue with:
- What you did
- What you expected
- What happened instead
- macOS version and whether you built Debug or Release

---

## License

By contributing you agree that your contribution will be licensed under the same [BSD 3-Clause License](LICENSE) that covers the project.
