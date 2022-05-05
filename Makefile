APP_NAME = e_annatel
PREFIX = /opt/mine

default: build/$(APP_NAME)

CFLAGS := -Wall -Wextra -Wshadow -Wno-type-limits -g3 -O0 -Wpointer-arith -fvisibility=hidden

CFLAGS += -DAPP_NAME=\"$(APP_NAME)\" -DPREFIX=\"$(PREFIX)\"

build/$(APP_NAME): main.c
	mkdir -p $(@D)
	gcc -g $^ $(CFLAGS) `pkg-config --cflags --libs elementary` -o $@

install: build/$(APP_NAME)
	mkdir -p $(PREFIX)/bin
	install -c build/$(APP_NAME) $(PREFIX)/bin/

clean:
	rm -rf build/
