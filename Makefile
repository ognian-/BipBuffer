rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))
uniq = $(if $1,$(firstword $1) $(call uniq,$(filter-out $(firstword $1),$1)))

PRODUCT := test_bip
CFLAGS := -std=c++11 -fexceptions -frtti -pthread -Wall -Wextra -Weffc++
LDFLAGS := 
OUTDIR := build

HEADERS := $(call rwildcard,include/,*.h)
SOURCES := $(call rwildcard,src/,*.cpp)
OBJECTS := $(patsubst %.cpp,%.o,$(patsubst %,$(OUTDIR)/%,$(SOURCES)))

CFLAGS += -Iinclude

all: $(OUTDIR)/$(PRODUCT)
$(OUTDIR)/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	@echo Compiling $<
	@g++ -c -o $@ $< $(CFLAGS)
$(OUTDIR)/$(PRODUCT): $(OBJECTS)
	@echo Linking $@
	@g++ -o $@ $^ $(CFLAGS) $(LDFLAGS)
	@echo Success
.PHONY: clean
clean:
	@rm -rf $(OUTDIR)
