# the lazy frontend for people who dont want to type cmake all day, `make help` lists whats in here
# uses ">" instead of tabs for recipes because tabs are stupid
.RECIPEPREFIX = >

PREFIX     ?= /usr/local
BUILD_DIR  ?= build
BUILD_TYPE ?= RelWithDebInfo
DIST_DIR   ?= dist
JOBS       ?= $(shell nproc 2>/dev/null || echo 2)

# the version lives in CMakeLists.txt and nowhere else
VERSION := $(shell sed -n 's/^project.rockcat-swarm VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
# sitting exactly on the release tag = plain version, anything else gets the commit count and hash glued on
# so a random dev build never pretends to be the release
GIT_TAG := $(shell git describe --tags --exact-match 2>/dev/null)
GIT_REV := $(shell git rev-list --count HEAD 2>/dev/null)
GIT_SHA := $(shell git rev-parse --short=7 HEAD 2>/dev/null)
ifeq ($(GIT_TAG),$(VERSION))
PKGVERSION := $(VERSION)
else ifneq ($(GIT_SHA),)
PKGVERSION := $(VERSION)+r$(GIT_REV).g$(GIT_SHA)
else
PKGVERSION := $(VERSION)
endif

# pacman has no idea whats installed on a non arch box, so it doesnt get asked there
MAKEPKG_FLAGS ?= $(if $(wildcard /etc/arch-release),,--nodeps)

.PHONY: all test check run install uninstall clean distclean deb arch pkg packages help

all:
> cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(PREFIX)
> cmake --build $(BUILD_DIR) -j $(JOBS)

test check: all
> ctest --test-dir $(BUILD_DIR) --output-on-failure

run: all
> $(BUILD_DIR)/rockcat-swarm

# does NOT build, on purpose. `sudo make install` on a fresh checkout would leave you with a root owned build dir, no bueno
install:
> @test -f $(BUILD_DIR)/cmake_install.cmake || { echo "nothing built yet, run 'make' first (without sudo please)"; exit 1; }
> cmake --install $(BUILD_DIR)
> @test -n "$(DESTDIR)" || echo "installed to $(PREFIX). replug your devices (or run 'udevadm control --reload && udevadm trigger' as root) so the udev rule kicks in"

uninstall:
> @test -f $(BUILD_DIR)/install_manifest.txt || { echo "no install_manifest.txt in $(BUILD_DIR), nothing i know how to remove"; exit 1; }
> sed 's|^|$(DESTDIR)|' $(BUILD_DIR)/install_manifest.txt | xargs -d '\n' rm -fv

clean:
> rm -rf -- "$(BUILD_DIR)"

distclean: clean
> rm -rf -- build-deb build-arch "$(DIST_DIR)"

# .deb through cpack, the settings sit at the bottom of CMakeLists.txt
deb:
> cmake -S . -B build-deb -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DROCKCAT_PACKAGE=deb -DROCKCAT_PACKAGE_VERSION=$(PKGVERSION)
> cmake --build build-deb -j $(JOBS)
> ctest --test-dir build-deb --output-on-failure
> rm -f build-deb/*.deb
> cd build-deb && cpack -G DEB
> mkdir -p $(DIST_DIR)
> cp build-deb/*.deb $(DIST_DIR)/
> @echo "Evoila! $$(ls $(DIST_DIR)/*_$(PKGVERSION)-*.deb)"

# pacman package through makepkg. works on arch and on anything else that has makepkg + bsdtar lying around
# the PKGBUILD gets generated into build-arch because makepkg loves scribbling into the folder its in
arch pkg:
> @command -v makepkg >/dev/null || { echo "no makepkg here (debian: apt install makepkg)"; exit 1; }
> @command -v bsdtar >/dev/null || { echo "makepkg needs bsdtar to pack the thing (debian: apt install libarchive-tools)"; exit 1; }
> rm -rf -- build-arch
> mkdir -p build-arch $(DIST_DIR)
> sed -e 's|@VERSION@|$(PKGVERSION)|' -e 's|@SRCDIR@|$(CURDIR)|' packaging/arch/PKGBUILD.in > build-arch/PKGBUILD
> cp aur/rockcat-swarm-git/rockcat-swarm.install build-arch/
> cd build-arch && PKGDEST=$(abspath $(DIST_DIR)) PKGEXT=.pkg.tar.zst makepkg -f $(MAKEPKG_FLAGS)
> @echo "Evoila! $$(ls $(DIST_DIR)/rockcat-swarm-$(PKGVERSION)-*.pkg.tar.zst)"

packages: deb arch

help:
> @echo "make              build into $(BUILD_DIR)/"
> @echo "make test         build + run the offline tests"
> @echo "make run          build + start the gui from the build dir"
> @echo "make install      install what was built (PREFIX=$(PREFIX), DESTDIR works too)"
> @echo "make uninstall    remove what make install put there"
> @echo "make clean        delete $(BUILD_DIR)/"
> @echo "make distclean    delete every build dir and $(DIST_DIR)/"
> @echo "make deb          $(DIST_DIR)/rockcat-swarm_$(PKGVERSION)-1_<arch>.deb"
> @echo "make arch         $(DIST_DIR)/rockcat-swarm-$(PKGVERSION)-1-<arch>.pkg.tar.zst"
> @echo "make packages     both of them"
> @echo
> @echo "knobs: PREFIX, DESTDIR, BUILD_DIR, BUILD_TYPE, DIST_DIR, JOBS"
