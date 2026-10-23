# Makefile for dripfeed
#
#   make            build bin/dripfeed
#   make test       build, then run the test suite (needs python3)
#   make clean      remove build products (bin/dripfeed, build/) and archives
#   make install    install to /usr/local/bin  (usually needs sudo)
#   make uninstall  remove the installed binary
#   make archive    zip the repository to dripfeed-<version>.zip
#
# Overrides:  make CC=clang   make DEBUG=1   make install PREFIX=/opt/local
#             make install DESTDIR=/tmp/stage     (staged install for packagers)

NAME     := dripfeed
VERSION  := $(shell sed -n 's/^\#define DF_VERSION  *"\(.*\)"/\1/p' inc/common.h)

PREFIX   ?= /usr/local
BINDIR   ?= $(PREFIX)/bin

SRC_DIR  := src
INC_DIR  := inc
OBJ_DIR  := build
BIN_DIR  := bin

CC       ?= cc
CFLAGS   ?= -O2
CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic -I$(INC_DIR)
ifdef DEBUG
CFLAGS   += -g -O0 -fsanitize=address,undefined
LDFLAGS  += -fsanitize=address,undefined
endif

SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)
TARGET   := $(BIN_DIR)/$(NAME)
ARCHIVE  := $(NAME)-$(VERSION).zip

.PHONY: all clean install uninstall archive test help

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

# -MMD -MP: generate header dependency files so editing inc/*.h rebuilds.
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(NAME)-*.zip

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(NAME)
	@echo "Installed $(DESTDIR)$(BINDIR)/$(NAME)"
	@echo "Tip: serial access usually needs the 'dialout' group: sudo usermod -aG dialout $$USER"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)

# Zip the repository (everything tracked or not, minus build products, VCS
# metadata and old archives) into a single top-level directory.
archive:
	@command -v zip >/dev/null 2>&1 || { echo "archive: 'zip' is not installed"; exit 1; }
	rm -f $(ARCHIVE)
	rm -rf $(OBJ_DIR)/.stage && mkdir -p $(OBJ_DIR)/.stage
	ln -s "$(CURDIR)" $(OBJ_DIR)/.stage/$(NAME)-$(VERSION)
	cd $(OBJ_DIR)/.stage && zip -qr "$(CURDIR)/$(ARCHIVE)" $(NAME)-$(VERSION) \
	    -x '*/.git/*' '*/$(OBJ_DIR)/*' '*/$(BIN_DIR)/*' '*/*.zip'
	rm -rf $(OBJ_DIR)/.stage
	@echo "Created $(ARCHIVE)"

test: $(TARGET)
	python3 -I tests/run_tests.py $(TARGET)

help:
	@sed -n '2,10p' Makefile | sed 's/^# \{0,1\}//'

-include $(DEPS)
