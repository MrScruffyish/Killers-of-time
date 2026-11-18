# Convenience wrapper around the CMake builds described in README.md.

CMAKE             ?= cmake
GENERATOR         ?= Ninja
JOBS              ?= $(shell nproc 2>/dev/null || echo 2)
BUILD_DIR         ?= build
PLAY_BUILD_DIR    ?= build-play
RELEASE_BUILD_DIR ?= build-release

.PHONY: all configure build test play release install package clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -G $(GENERATOR) -DCMAKE_BUILD_TYPE=Debug

build: configure
	$(CMAKE) --build $(BUILD_DIR) -j$(JOBS)

test: build
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
