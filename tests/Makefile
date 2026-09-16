# Host test suites for the Gyrovault pure-C core. Run under WSL: make run
CC      = gcc
CFLAGS  = -I../src -Wall -Wextra -Werror -O1
LDLIBS  = -lm

# solve_core.c is a test-side helper, not game code; it links into every suite so the
# pattern rule below stays a one-liner. Only test_solvable calls it.
CORE    = ../src/level.c ../src/physics.c ../src/save.c ../src/version.c \
          $(wildcard ../src/motion_map.c) $(wildcard ../src/sound_synth.c) \
          $(wildcard ../src/timer.c) solve_core.c
HEADERS = $(wildcard ../src/*.h) test.h solve_core.h
TESTS   = $(sort $(wildcard test_*.c))
SUITES  = $(TESTS:.c=)
BINS    = $(addprefix build/,$(SUITES))

.PHONY: all run clean

all: $(BINS)

build/%: %.c $(CORE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $< $(CORE) $(LDLIBS)

# Builds and runs every suite; a build failure or a failing suite makes the exit non-zero.
run:
	@fail=0; count=0; \
	for s in $(SUITES); do \
	  count=$$((count+1)); \
	  if ! $(MAKE) --no-print-directory build/$$s; then \
	    echo "SUITE $$s: BUILD FAILED"; fail=1; continue; \
	  fi; \
	  if ./build/$$s; then echo "SUITE $$s: PASS"; \
	  else echo "SUITE $$s: FAIL"; fail=1; fi; \
	done; \
	if [ $$count -eq 0 ]; then echo "NO SUITES FOUND"; fail=1; fi; \
	if [ $$fail -eq 0 ]; then echo "ALL $$count SUITES PASSED"; \
	else echo "TESTS FAILED ($$count suites)"; fi; \
	exit $$fail

clean:
	rm -rf build
