#!/usr/bin/make -f
# DPF inner Makefile for MyPlugin
# -------------------------------

# Set BETA=1 (or MYPLUGIN_BETA=1) on the make command line for the
# side-by-side beta variant: distinct LV2 URI, bundle name, brand and
# unique id. The top-level Makefile wraps this in `make beta`.
ifeq ($(BETA),1)
MYPLUGIN_BETA = 1
endif
ifeq ($(MYPLUGIN_BETA),1)
NAME = myplugin-beta
# DPF's Makefile.base.mk hard-resets BUILD_CXX_FLAGS but APPENDS
# $(CXXFLAGS) at the end, so user-visible flags must travel through
# CXXFLAGS to actually reach the compiler. Putting -DMYPLUGIN_BETA=1
# in BUILD_CXX_FLAGS silently fails.
CXXFLAGS += -DMYPLUGIN_BETA=1
else
NAME = myplugin
endif

# Plugin version — the top-level VERSION file is the single source of
# truth (`make release` bumps and commits it to match the release tag).
# Injected as preprocessor defines so getVersion() and the generated LV2
# TTL (lv2:minorVersion / lv2:microVersion) always report the released
# version. Travels through CXXFLAGS for the same reason as the BETA
# macro above. The macro names are deliberately NOT plugin-prefixed so
# forks don't need to rename them.
PLUGIN_VERSION := $(strip $(shell cat ../../VERSION 2>/dev/null))
ifeq ($(PLUGIN_VERSION),)
$(error VERSION file not found at ../../VERSION)
endif
PLUGIN_VERSION_PARTS := $(subst ., ,$(PLUGIN_VERSION))
CXXFLAGS += -DPLUGIN_VERSION_MAJOR=$(word 1,$(PLUGIN_VERSION_PARTS)) \
            -DPLUGIN_VERSION_MINOR=$(word 2,$(PLUGIN_VERSION_PARTS)) \
            -DPLUGIN_VERSION_MICRO=$(word 3,$(PLUGIN_VERSION_PARTS))

# Source files (DPF's makefile expects FILES_DSP for DSP-only plugins,
# FILES_UI when there's a native UI — but we use modgui for the MOD GUI,
# so DSP-only here).
FILES_DSP = \
	MyPluginPlugin.cpp

# Pull in DPF's shared build rules
include ../../dpf/Makefile.plugins.mk

# Targets — LV2 only (MOD Desktop / MOD Dwarf both consume LV2).
# Add `vst2`, `vst3`, `clap`, etc. here if you want extra formats; each
# adds DPF code with potentially different licensing — see
# `dpf/LICENSING.md`.
TARGETS += lv2

all: $(TARGETS)
