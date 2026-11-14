# Contributing

Issues and pull requests are welcome.

## The one rule

**Nothing from the game goes in the repository.** No game files, no rebased
modules or catalogs (`work/`), no lifted C (`src/recomp/gen/`), no
disassembly listings, nothing generated from `client.exe` or the game's
DLLs. The tool ships; its output is made on each person's machine from their
own copy. `.gitignore` covers the usual paths; check `git status` before you
commit anyway. Screenshots of the game running are fine.

## Before a pull request

1. Build from a clean lift (pcrecomp main, beside this repo):
   ```
   py -3 tools\rebase.py
   py -3 tools\catalog.py
   py -3 run_lift.py
   build.cmd
   ```
   Expected from the lift: `lift errors 0`.
2. Run the checks in [docs/testing.md](docs/testing.md): the dry run (every
   module `0 unresolved`) and the headless run into the first level. There is
   no conformance harness yet, so say in the PR which runs you did and attach
   the `--shot` pictures.
3. Where the lift and the shipping code disagree, say which:
   `--native MODULE` runs that module's original machine code under the same
   host.

## Where a fix belongs

- A bug in the lifter, the catalog, the loader or `native32` belongs in
  [pcrecomp](https://github.com/sp00nznet/pcrecomp), not here.
- A fix to the game itself is a patch in `run_lift.py` or a shim in
  `src/runtime/host.c`, with a comment that says what it fixes and why.
- A wall you got past goes in `docs/bringup.md`: what it looked like, what it
  was, what moved it.

## Where your code comes from

Contributions must be your own or MIT-compatible. Don't port code from GPL
or LGPL projects (Wine, ReactOS, DOSBox, emulators): reading their source
and writing "our own" version makes it theirs.
