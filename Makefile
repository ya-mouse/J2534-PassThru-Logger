# J2534 PassThru Logger — Unified Build
# Usage:
#   make              — build all (Release)
#   make DEBUG=1      — build all (Debug)
#   make dll          — build C++ DLL only (via Docker)
#   make control      — build PassThruLoggerControl only
#   make sample       — build SampleClient only
#   make clean        — remove all build artifacts

# Configuration
DEBUG ?= 0
ifeq ($(DEBUG),1)
  CONFIG = Debug
  DOTNET_CONFIG = Debug
  MINGW_OPT = -O0 -g -DDEBUG
else
  CONFIG = Release
  DOTNET_CONFIG = Release
  MINGW_OPT = -O2 -DNDEBUG
endif

OUTDIR = build/$(CONFIG)
DOCKER_IMAGE = j2534-builder
# Detect dotnet: prefer PATH, fall back to common macOS location
DOTNET ?= $(shell command -v dotnet 2>/dev/null || echo /usr/local/share/dotnet/dotnet)
RID ?= win-x64

# .NET publish flags
DOTNET_PUBLISH = $(DOTNET) publish -c $(DOTNET_CONFIG) -r $(RID) --self-contained \
	-p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true \
	--nologo -v quiet

.PHONY: all dll control sample clean docker-image

all: dll control sample

# ─── C++ DLL (Docker + mingw-w64) ────────────────────────────────────────────

dll: docker-image
	@mkdir -p $(OUTDIR)/PassThruLogger
	docker run --rm -v "$(CURDIR):/src" $(DOCKER_IMAGE) \
		make -f Makefile.mingw \
		CC=i686-w64-mingw32-g++ \
		OUTDIR=$(OUTDIR)/PassThruLogger \
		EXTRA_CXXFLAGS="$(MINGW_OPT)"
	@echo "→ $(OUTDIR)/PassThruLogger/PassThruLogger.dll"

docker-image:
	@docker inspect $(DOCKER_IMAGE) >/dev/null 2>&1 || \
		docker build -f Dockerfile.mingw -t $(DOCKER_IMAGE) .

# ─── C# PassThruLoggerControl ────────────────────────────────────────────────

control:
	@mkdir -p $(OUTDIR)/PassThruLoggerControl
	$(DOTNET_PUBLISH) -o $(OUTDIR)/PassThruLoggerControl \
		PassThruLoggerControl/PassThruLoggerControl.csproj
	@echo "→ $(OUTDIR)/PassThruLoggerControl/"

# ─── C# SampleClient ─────────────────────────────────────────────────────────

sample:
	@mkdir -p $(OUTDIR)/SampleClient
	$(DOTNET_PUBLISH) -o $(OUTDIR)/SampleClient \
		SampleClient/SampleClient.csproj
	@echo "→ $(OUTDIR)/SampleClient/"

# ─── Clean ────────────────────────────────────────────────────────────────────

clean:
	rm -rf build
