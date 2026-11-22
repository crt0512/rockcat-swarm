# AUR packages

| Directory | Package | Builds from |
|---|---|---|
| `rockcat-swarm-bin/` | `rockcat-swarm-bin` | the x86_64 tarball attached to the GitHub release (`.github/workflows/release.yml`) |
| `rockcat-swarm-git/` | `rockcat-swarm-git` | the latest commit of https://github.com/crt0512/rockcat-swarm |

Both depend on `hidapi`, `gtk3`, `glib2`, `cairo`, `gdk-pixbuf2` and `libayatana-appindicator` (system tray), conflict with each other and provide `rockcat-swarm`. They also conflict with the old `rossat-swarm-bin` / `rossat-swarm-git` packages, two lighting engines fighting over the same mouse is no bueno. The shared `rockcat-swarm.install` reloads the udev rules and points at the lighting engine user service.

Not on the AUR yet under the new name, and theres no makepkg on the box these files were edited on, so the `.SRCINFO` files were updated by hand. Regenerate them properly before the first push.

Releasing a new version:

1. Bump `project(... VERSION x.y.z)` in `CMakeLists.txt` and the
   `ROCKCAT_VERSION_*` macros in `include/rockcat/rockcat.h`, commit, tag `x.y.z`, push.
2. Wait for the release workflow to attach `rockcat-swarm-x.y.z-x86_64.tar.gz`.
3. In `rockcat-swarm-bin/`: set `pkgver`, run `updpkgsums` (replaces the
   `SKIP` checksum) and `makepkg --printsrcinfo > .SRCINFO`, then push to the AUR.
4. `rockcat-swarm-git` picks up new commits by itself; regenerate its
   `.SRCINFO` only when the PKGBUILD changes.
