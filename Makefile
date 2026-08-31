# Makefile -- builds the flightnet CLI and its test binary.
#
# Works with GNU make and with mingw32-make on Windows.
#
#   make            build both binaries
#   make test       build and run the unit tests
#   make run        build, then run the guided demo
#   make bench      the full 10,000-network stress run
#   make scaling    Ford-Fulkerson vs Dinic across network sizes
#   make web        browser UI at http://localhost:8000
#   make clean      remove build output
#
# Override the compiler or flags on the command line, e.g.
#   make CXX=clang++ CXXSTD=-std=c++17

CXX      ?= g++
CXXSTD   ?= -std=c++14
OPT      ?= -O2
WARN     ?= -Wall -Wextra
CXXFLAGS ?= $(CXXSTD) $(OPT) $(WARN) -Iinclude

BUILD   := build
SRCDIR  := src
TESTDIR := tests

ifeq ($(OS),Windows_NT)
  EXEEXT := .exe
else
  EXEEXT :=
endif

APP   := $(BUILD)/flightnet$(EXEEXT)
TESTS := $(BUILD)/flightnet_tests$(EXEEXT)

# Every library object. main.cpp is kept out of this list so the test binary can
# link the library without pulling in a second main().
LIB_SRCS := $(SRCDIR)/graph.cpp \
            $(SRCDIR)/routing.cpp \
            $(SRCDIR)/maxflow.cpp \
            $(SRCDIR)/capacity.cpp \
            $(SRCDIR)/generator.cpp \
            $(SRCDIR)/loader.cpp \
            $(SRCDIR)/json_output.cpp \
            $(SRCDIR)/benchmark.cpp

LIB_OBJS  := $(patsubst $(SRCDIR)/%.cpp,$(BUILD)/%.o,$(LIB_SRCS))
TEST_SRCS := $(wildcard $(TESTDIR)/*.cpp)
TEST_OBJS := $(patsubst $(TESTDIR)/%.cpp,$(BUILD)/test_%.o,$(TEST_SRCS))

.PHONY: all app tests test run demo bench scaling web clean help

all: app tests

app: $(APP)

tests: $(TESTS)

# The build directory is tracked in git (build/.gitkeep), so this rule is only a
# safety net. The leading '-' keeps a failure on either shell from stopping make.
$(BUILD):
	-@mkdir $(BUILD)

$(BUILD)/%.o: $(SRCDIR)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/test_%.o: $(TESTDIR)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(TESTDIR) -c $< -o $@

$(APP): $(LIB_OBJS) $(BUILD)/main.o
	$(CXX) $^ -o $@

$(TESTS): $(LIB_OBJS) $(TEST_OBJS)
	$(CXX) $^ -o $@

# Header changes should force a rebuild; the dependency list is small enough to
# state directly rather than generating .d files.
$(LIB_OBJS) $(TEST_OBJS) $(BUILD)/main.o: $(wildcard include/flightnet/*.hpp)

# --- convenience targets ---------------------------------------------------

test: $(TESTS)
	@$(TESTS)

run demo: $(APP)
	@$(APP) demo

bench: $(APP)
	@$(APP) bench --networks 10000 --csv $(BUILD)/bench.csv

scaling: $(APP)
	@$(APP) bench --scaling --networks 200

# Browser front end. The server shells out to $(APP), so the page always shows
# what the compiled engine computed.
web: $(APP)
	@python web/server.py

clean:
	-@rm -rf $(BUILD)/*.o $(APP) $(TESTS)

help:
	@echo "targets: all app tests test run bench scaling web clean"
