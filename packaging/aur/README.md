# AUR packaging

This directory is the source for the stable `acclorite` AUR package.

The package deliberately follows a fixed upstream release tag rather than the development branch. For `0.3.3`, the source is `v0.3.3`. Network access is therefore a build/install concern only; the installed Acclorite runtime remains offline.

Before publishing a release to AUR:

1. push the corresponding upstream release commit and signed/annotated tag if used;
2. verify `git ls-remote --tags https://github.com/kzahed610/Acclorite.git v<version>` resolves the intended tag;
3. run `makepkg -si` in this directory on a clean Arch/CachyOS environment;
4. run `namcap` on both `PKGBUILD` and the resulting package when available;
5. regenerate `.SRCINFO` with `makepkg --printsrcinfo > .SRCINFO` after every PKGBUILD change;
6. commit only `PKGBUILD` and `.SRCINFO` to the AUR package repository.

The current PKGBUILD uses a fixed Git tag. Arch's VCS source support permits a statically versioned package to fetch a specific tag; `git` is therefore a build dependency and the source checksum is skipped for that VCS source. Do not change the source to a moving branch for the stable package.
