# Klipper-Pro packaging.
#
# `make package` produces ./com.orcaslicer.klipper-pro-0.1.0.orcaplugin
# (zip) containing:
#   plugin.json
#   settings.schema.json
#   <id>.so / .dll / .dylib (cargo build --release output)
#   ui/index.html, ui/assets/* (vite build output)

ID      := com.orcaslicer.klipper-pro
VERSION := 0.1.0
TARGET  := target/release
PKG     := $(ID)-$(VERSION).orcaplugin

# Platform extension auto-detect.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
EXT := dylib
PFX := lib
else ifeq ($(OS),Windows_NT)
EXT := dll
PFX :=
else
EXT := so
PFX := lib
endif

CARGO_LIB := $(TARGET)/$(PFX)klipper_pro.$(EXT)
STAGED    := staged/$(ID)

.PHONY: all build-rust build-ui stage package clean

all: package

build-rust:
	cargo build --release

build-ui:
	cd ui && pnpm install --frozen-lockfile && pnpm run build

stage: build-rust build-ui
	rm -rf $(STAGED)
	mkdir -p $(STAGED)/ui
	cp plugin.json           $(STAGED)/manifest.json
	cp settings.schema.json  $(STAGED)/
	cp $(CARGO_LIB)          $(STAGED)/$(ID).$(EXT)
	cp -R ui/dist/.          $(STAGED)/ui/

package: stage
	cd staged && zip -r ../$(PKG) $(ID)
	@echo "Packaged $(PKG)"

clean:
	rm -rf target staged ui/dist $(PKG)
