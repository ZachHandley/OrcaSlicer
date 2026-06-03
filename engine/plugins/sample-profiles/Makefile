# OrcaForge Showcase Profile Pack — data-plugin packaging.
#
# `make build` produces target/sample-profiles/com.orcaslicer.sample-profiles.orcaplugin
# (a zip of manifest.json + profiles/ + README.md). No native compile step —
# this is a kind=data plugin (JSON only).

ID       := com.orcaslicer.sample-profiles
HERE     := $(abspath .)
OUT_DIR  := $(HERE)/target/sample-profiles
PKG      := $(OUT_DIR)/$(ID).orcaplugin
STAGED   := $(OUT_DIR)/staged/$(ID)

.PHONY: all build stage clean

all: build

stage:
	rm -rf $(STAGED)
	mkdir -p $(STAGED)
	cp $(HERE)/manifest.json $(STAGED)/
	cp $(HERE)/README.md $(STAGED)/
	cp -R $(HERE)/profiles $(STAGED)/

build: stage
	mkdir -p $(OUT_DIR)
	rm -f $(PKG)
	cd $(STAGED) && zip -r $(PKG) .
	@echo "Packaged $(PKG)"

clean:
	rm -rf target
