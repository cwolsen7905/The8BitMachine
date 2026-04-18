# TODO

Outstanding work items not yet scheduled for a release.

---

## macOS / Packaging
- [ ] Dock name still shows `the-8-bit-machine` — LS cache flush or `.app` rename needed
- [ ] Add `.icns` file so Finder/Dock show the logo instead of a generic icon
- [ ] DMG packaging for macOS releases

## ZX Spectrum
- [ ] Validate keyboard input in BASIC (typing, line editing, program entry)
- [ ] Tape loading — `.tap` / `.tzx` file support
- [ ] ULA port read EAR/MIC bits (currently always returns no signal)
- [ ] Save/load machine config round-trip for the Spectrum preset

## C64
- [ ] Validate SID audio after Spectrum work
- [ ] Datasette / disk drive emulation

## Machine Designer
- [ ] Port I/O handler wiring for custom Z80 machines (IN/OUT routing in designer)

## Debugger
- [ ] Breakpoint persistence across sessions

## Infrastructure
- [ ] CI build for Windows and Linux
- [ ] README: document keyboard shortcuts and designer workflow
