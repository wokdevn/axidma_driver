# examples.mk
#
# Date: March 6, 2016
# Author: Brandon Perez
# Author: Jared Choi

# Include guard for the Makefile
ifndef EXAMPLES_MAKEFILE_
EXAMPLES_MAKEFILE_=included

# Get the definitions for the AXI DMA library
include library/library.mk

################################################################################
# Configuration
################################################################################

# The list of example programs
EXAMPLES_DIR = examples
EXAMPLES_FILES = axidma_benchmark.c axidma_display_image.c axidma_transfer.c \
					test_axidma.c axidma_test.c dma_uart.c interdma.c mgpio.c \
					irqdma.c rirqdma.c  tcpclient.c

# The variations of specific targets for the example programs
EXAMPLES_TARGETS = $(EXAMPLES_FILES:%.c=%)
EXAMPLES_CLEAN_TARGETS = $(addsuffix _clean,$(EXAMPLES_TARGETS))
EXAMPLES_EXECUTABLES = $(addprefix $(EXAMPLES_DIR)/,$(EXAMPLES_TARGETS))
EXAMPLES_OUTPUT_EXECUTABLES = $(addprefix $(OUTPUT_DIR)/,$(EXAMPLES_TARGETS))

# The local helper function files used across the example programs.
UTIL_DIR = $(EXAMPLES_DIR)
UTIL_FILES = util.c udpsend.c util_interdma.c mgpio.c tcpserver.c
UTIL = $(addprefix $(UTIL_DIR)/,$(UTIL_FILES))

# The compiler flags used to compile the examples
EXAMPLES_CFLAGS = $(GLOBAL_CFLAGS)   -Wno-unused-variable  \
				-Wno-unused-parameter -Wno-unused-but-set-variable

# Set the example executables to link against the AXI DMA shared library in
# the outputs directory
EXAMPLES_LINKER_FLAGS = -Wl,-rpath,'$$ORIGIN'
EXAMPLES_LIB_FLAGS = -L $(OUTPUT_DIR) -l $(LIBAXIDMA_NAME) \
					 $(EXAMPLES_LINKER_FLAGS)

################################################################################
# Targets
################################################################################

# These targets don't correspond to actual generated files
.PHONY: all examples examples_clean $(EXAMPLES_TARGETS) \
		$(EXAMPLES_CLEAN_TARGETS)

# Allow for secondary expansion in prerequisite lists. This allows for automatic
# variables (e.g. $@, $^, etc.) to be used in prerequisite lists.
.SECONDEXPANSION:

# Build all of the example files
examples: $(EXAMPLES_TARGETS)

# User-facing targets for compiling examples
$(EXAMPLES_TARGETS): $(OUTPUT_DIR)/$$@

# Compile a given example into an executable. This target does not need re-run
# because of the check target nor the AXI DMA shared library object.
$(EXAMPLES_EXECUTABLES): $$@.c $(UTIL) | $(LIBAXIDMA_OUTPUT_LIBRARY) \
						 cross_compiler_check
	$(CC) $(EXAMPLES_CFLAGS) $(LIBAXIDMA_INC_FLAGS) $(filter %.c,$^) -o $@ \
		$(EXAMPLES_LIB_FLAGS)

# Copy a compiled example executable to the specified output directory
$(EXAMPLES_OUTPUT_EXECUTABLES): $(EXAMPLES_DIR)/$$(shell basename $$@) \
							    $(OUTPUT_DIR)
	@cp $< $@

# Clean up all the files generated by compiling the examples
examples_clean: $(EXAMPLES_CLEAN_TARGETS)

# Clean a specific example by deleting both copies of the executable
$(EXAMPLES_CLEAN_TARGETS):
	rm -f $(EXAMPLES_OUTPUT_EXECUTABLES) $(EXAMPLES_EXECUTABLES)

endif # EXAMPLES_MAKEFILE_
