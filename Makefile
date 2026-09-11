CC = i686-w64-mingw32-gcc
STRIP = i686-w64-mingw32-strip
CFLAGS = -O2 -Wall -Wextra
LDFLAGS = -static -static-libgcc
MINHOOK_SOURCES = third_party/minhook/src/buffer.c third_party/minhook/src/hook.c \
 third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde32.c
MINHOOK_CFLAGS = -Ithird_party/minhook/include

.PHONY: release test-build clean

release:
	mkdir -p release
	$(CC) $(CFLAGS) $(LDFLAGS) -mwindows -o release/mvp2005fix.exe src/launcher.c src/injection.c -lcomdlg32
	$(CC) $(CFLAGS) $(MINHOOK_CFLAGS) $(LDFLAGS) -shared -o release/mvp2005fix.dll src/runtime.c $(MINHOOK_SOURCES)
	$(STRIP) release/mvp2005fix.exe release/mvp2005fix.dll

test-build:
	mkdir -p release
	$(CC) $(CFLAGS) $(MINHOOK_CFLAGS) $(LDFLAGS) -o release/inline-hooks-test.exe tests/inline_hooks.c $(filter-out third_party/minhook/src/hook.c,$(MINHOOK_SOURCES))
	$(CC) $(CFLAGS) $(LDFLAGS) -o release/startup-test.exe tests/startup.c
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o release/startup-fixture.dll tests/startup_fixture.c

clean:
	rm -f release/mvp2005fix.exe release/mvp2005fix.dll release/inline-hooks-test.exe release/startup-test.exe release/startup-fixture.dll
