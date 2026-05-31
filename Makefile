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

# .NET publish flags (framework-dependent — requires .NET 8 runtime on target)
DOTNET_PUBLISH = $(DOTNET) publish -c $(DOTNET_CONFIG) -r $(RID) --self-contained false \
	--nologo -v quiet

.PHONY: all dll kvaser control sample clean docker-image test-kvaser

all: dll kvaser control sample

# ─── C++ DLL (Docker + mingw-w64) ────────────────────────────────────────────

dll: docker-image
	@mkdir -p $(OUTDIR)
	docker run --rm -v "$(CURDIR):/src" $(DOCKER_IMAGE) \
		make -f Makefile.mingw \
		CC=i686-w64-mingw32-g++ \
		OUTDIR=$(OUTDIR) \
		EXTRA_CXXFLAGS="$(MINGW_OPT)"
	@echo "→ $(OUTDIR)/PassThruLogger.dll"

# ─── KvaserDirect DLL (Docker + mingw-w64) ───────────────────────────────────

kvaser: docker-image
	@mkdir -p $(OUTDIR)
	docker run --rm -v "$(CURDIR):/src" $(DOCKER_IMAGE) \
		make -f KvaserDirect/Makefile.mingw \
		OUTDIR=$(OUTDIR) \
		EXTRA_CXXFLAGS="$(MINGW_OPT)"
	@cp KvaserDirect/install.reg KvaserDirect/uninstall.reg $(OUTDIR)/
	@echo "→ $(OUTDIR)/KvaserDirect.dll + install.reg + uninstall.reg"

docker-image:
	@docker inspect $(DOCKER_IMAGE) >/dev/null 2>&1 || \
		docker build -f Dockerfile.mingw -t $(DOCKER_IMAGE) .

# ─── C# PassThruLoggerControl ────────────────────────────────────────────────

control:
	@mkdir -p $(OUTDIR)
	$(DOTNET_PUBLISH) -o $(OUTDIR) \
		PassThruLoggerControl/PassThruLoggerControl.csproj
	@echo "→ $(OUTDIR)/PassThruLoggerControl.exe"

# ─── C# SampleClient ─────────────────────────────────────────────────────────

sample:
	@mkdir -p $(OUTDIR)
	$(DOTNET_PUBLISH) -o $(OUTDIR) \
		SampleClient/SampleClient.csproj
	@echo "→ $(OUTDIR)/AsynchronousClient.exe"

# ─── Clean ────────────────────────────────────────────────────────────────────

clean:
	rm -rf build

# ─── KvaserDirect Unit Tests (compile only — run on Windows or with Wine) ────

test-kvaser: docker-image
	docker run --rm -v "$(CURDIR):/src" $(DOCKER_IMAGE) \
		make -f KvaserDirect/tests/Makefile.test
	@echo "→ build/tests/test_isotp.exe (run on Windows or via Wine)"
