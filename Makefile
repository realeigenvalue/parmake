OBJS_DIR = .objs

# define all the executables
EXE_PARMAKE=parmake
EXES_APPLICATION=$(EXE_PARMAKE)

# list object file dependencies for each
OBJS_PARMAKE=parmake.o parser.o rule.o queue.o parmake_main.o

# set up compiler
CC = gcc
WARNINGS = -Wall -Wextra -Werror -Wno-error=unused-parameter
INC=
CFLAGS_DEBUG   = -O0 $(WARNINGS) $(INC) -g -std=c99 -c -MMD -MP -D_GNU_SOURCE -pthread
CFLAGS_RELEASE = -O2 $(WARNINGS) $(INC) -g -std=c99 -c -MMD -MP -D_GNU_SOURCE -pthread

# tsan needs some funky flags
CFLAGS_TSAN    = $(CFLAGS_DEBUG)
CFLAGS_TSAN    += -fsanitize=thread -DSANITIZE_THREADS -fPIC

# set up linker
LD = gcc
LDFLAGS = -lcrypt -pthread
LDFLAGS_TSAN = $(LDFLAGS) -ltsan -pie

.PHONY: all
all: release

# build types
# run clean before building debug so that all of the release executables
# disappear
.PHONY: debug
.PHONY: release
.PHONY: tsan

release: $(EXES_APPLICATION)
debug:   clean $(EXES_APPLICATION:%=%-debug)
tsan:    clean $(EXES_APPLICATION:%=%-tsan)

# include dependencies
-include $(OBJS_DIR)/*.d

$(OBJS_DIR):
	@mkdir -p $(OBJS_DIR)

# patterns to create objects
# keep the debug and release postfix for object files so that we can always
# separate them correctly
$(OBJS_DIR)/%-debug.o: %.c | $(OBJS_DIR)
	@mkdir -p $(basename $@)
	$(CC) $(CFLAGS_DEBUG) $< -o $@

$(OBJS_DIR)/%-tsan.o: %.c | $(OBJS_DIR)
	@mkdir -p $(basename $@)
	$(CC) $(CFLAGS_TSAN) $< -o $@

$(OBJS_DIR)/%-release.o: %.c | $(OBJS_DIR)
	@mkdir -p $(basename $@)
	$(CC) $(CFLAGS_RELEASE) $< -o $@

# exes
# you will need a triple of exe and exe-debug and exe-tsan for each exe (other
# than provided exes)
$(EXE_PARMAKE): $(OBJS_PARMAKE:%.o=$(OBJS_DIR)/%-release.o)
	$(LD) $^ $(LDFLAGS) -o $@

$(EXE_PARMAKE)-debug: $(OBJS_PARMAKE:%.o=$(OBJS_DIR)/%-debug.o)
	$(LD) $^ $(LDFLAGS) -o $@

$(EXE_PARMAKE)-tsan: $(OBJS_PARMAKE:%.o=$(OBJS_DIR)/%-tsan.o)
	$(LD) $^ $(LDFLAGS_TSAN) -o $@

.PHONY: clean
clean:
	-rm -rf .objs $(EXES_APPLICATION) $(EXES_APPLICATION:%=%-tsan) $(EXES_APPLICATION:%=%-debug) $(EXES_PROVIDED) $(EXES_OPTIONAL)