# Notice

The MIT license in [LICENSE](LICENSE) covers what is ours in this repository:
the host, the lift driver, the setup scripts, the tools and the
documentation. It does not cover the game.

*KISS Psycho Circus: The Nightmare Child* is © 2000 Third Law Interactive and
Gathering of Developers; KISS is a trademark of its owners; LithTech is ©
Monolith Productions. They are not ours to license. The game's executables,
modules, rez files, music and art are not in this repository and are
excluded by `.gitignore`.

- You need your own copy of the game, patched to 1.13, to use any of this.
  Setup links your folder as `game/`; the pipeline reads it from there.
- Lifted code is a derivative work of the binaries it came from. Anything the
  pipeline writes into `work/` or `src/recomp/gen*/` carries whatever license
  the original carries, not this one. The MIT grant stops at our own work.

This repository started from the
[Shogo recomp](https://github.com/sp00nznet/shogo-recomp), whose module
loader came from the [Unreal Tournament recomp](https://github.com/sp00nznet/ut99-recomp),
and runs on [pcrecomp](https://github.com/sp00nznet/pcrecomp)'s `native32`
runtime: all three are our own, MIT. No third-party code is vendored here.
The game's own third-party DLLs (Miles `mss32.dll`, Smacker `smackw32.dll`)
are loaded from your copy at run time and never enter the repository.
