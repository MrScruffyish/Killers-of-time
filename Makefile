# Convenience wrapper around the CMake builds described in README.md.
#
# Targets at a glance:
#   make / make all   EVERYTHING: Debug build + art refresh + play and Release builds
#   make build        Debug build in build/ only — no art refresh, no tests
#   make art          Debug build + refresh data/art if inputs changed — no tests
#   make test         Debug build + art refresh + ctest in build/
#   make play         RelWithDebInfo build in build-play/ only
#   make play_to_get_debug   Build play, run the client with verbose logging, save play-<time>.log
#   make release      Release build in build-release/ only
#   make install      Install the existing Release build
#   make package      Release build, then a .deb in build-release/
#   make clean        Delete all three build folders
#
# Plain `make` builds all three variants; `make XXX` builds only XXX.
# Art is refreshed by `make`, `make art` and `make test` (the Debug build owns the exporter).

CMAKE             ?= cmake
GENERATOR         ?= Ninja
JOBS              ?= $(shell nproc 2>/dev/null || echo 2)
BUILD_DIR         ?= build
PLAY_BUILD_DIR    ?= build-play
RELEASE_BUILD_DIR ?= build-release

ART_STAMP   := $(BUILD_DIR)/.art-stamp
ART_SOURCES := $(shell find data -type f -name '*.json' 2>/dev/null) \
               $(shell find src/artgen -type f \( -name '*.cpp' -o -name '*.h' \) 2>/dev/null) \
               src/client/ArtPaths.h src/tools/kot_artexport.cpp

.PHONY: all configure build art test play play_to_get_debug release install package clean

all: art play release

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Debug

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j$(JOBS)

# Generated art is data, not a build product, but it must match the content and the
# drawings. Only data/art is rewritten, so hand-made pictures in data/custom_art are
# safe. The stamp keeps this off incremental builds; touching any input redoes it.
art: build $(ART_STAMP)

$(ART_STAMP): $(ART_SOURCES) $(BUILD_DIR)/bin/kot_artexport
	@echo "Refreshing generated art in data/art (data/custom_art is untouched)..."
	@tmp=$$(mktemp -d) && \
	  $(BUILD_DIR)/bin/kot_artexport --out "$$tmp" && \
	  rm -rf data/art && \
	  mv "$$tmp" data/art && \
	  touch $@

test: art
	@sha=$$(git rev-parse HEAD); dirty=$$(git status --porcelain --untracked-files=no); \
	  if [ -n "$$dirty" ]; then \
	    patch=$$(git diff HEAD | git hash-object --stdin); \
	    echo "make test: commit $$sha + working-tree $$patch (dirty - $(MAKE) rebuilt from local changes)"; \
	  else \
	    echo "make test: commit $$sha (clean)"; \
	  fi | tee last_test
	@bash -o pipefail -c 'ctest --test-dir $(BUILD_DIR) --output-on-failure --stop-on-failure 2>&1 | tee -a last_test'

# Run the play client with verbose Qt logging and keep everything it prints in a
# timestamped log in the project root (crash backtraces also go to crash.log).
play_to_get_debug: play
	@bash -o pipefail -c 'log="play-$$(date +%Y%m%d-%H%M%S).log"; echo "Logging to $$log (set QT_LOGGING_RULES to change the filter)"; \
	  QT_LOGGING_RULES="$${QT_LOGGING_RULES:-kot.*.debug=true}" ./$(PLAY_BUILD_DIR)/bin/kot_client 2>&1 | tee "$$log"'

play:
	$(CMAKE) -S . -B $(PLAY_BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=RelWithDebInfo
	$(CMAKE) --build $(PLAY_BUILD_DIR) -j$(JOBS)

release:
	$(CMAKE) -S . -B $(RELEASE_BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build $(RELEASE_BUILD_DIR) -j$(JOBS)

install:
	$(CMAKE) --install $(RELEASE_BUILD_DIR)

package: release
	cd $(RELEASE_BUILD_DIR) && cpack -G DEB

clean:
	rm -rf $(BUILD_DIR) $(PLAY_BUILD_DIR) $(RELEASE_BUILD_DIR)
