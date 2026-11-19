# Convenience wrapper around the CMake builds described in README.md.

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

.PHONY: all configure build art test play release install package clean

all: art

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
	@bash -o pipefail -c 'ctest --test-dir $(BUILD_DIR) --output-on-failure --stop-on-failure 2>&1 | tee last_test'

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
