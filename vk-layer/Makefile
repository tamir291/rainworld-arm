# Needs gcc and Vulkan headers (package libvulkan-dev).
.PHONY: all clean
all: libVkLayer_opi_fakefeatures.so

libVkLayer_opi_fakefeatures.so: fakefeatures.c
	$(CC) -O2 -shared -fPIC -fvisibility=hidden -Wall -o $@ $<

clean:
	rm -f libVkLayer_opi_fakefeatures.so
