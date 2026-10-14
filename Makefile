# To build on the phone via Termux:
# $ pkg install clang make vulkan-headers
# $ make
#
# CC, CFLAGS, CPPFLAGS and LDFLAGS from the environment are respected, so the
# .deb packaging (and other package builders) can cross-compile it.

CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -pthread
LDLIBS  = -ldl -lm -pthread
PREFIX  ?= /usr/local
SRC     = src/main.c src/util.c src/monitor.c src/cpuload.c src/gpuload.c

socsonde: $(SRC) src/common.h src/fma_spv.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

install: socsonde
	install -Dm755 socsonde $(DESTDIR)$(PREFIX)/bin/socsonde

# Only needed if you edit shaders/fma.comp (needs glslangValidator + python3)
shader:
	glslangValidator -V --target-env vulkan1.0 shaders/fma.comp -o fma.spv
	python3 -c "import struct;d=open('fma.spv','rb').read();w=struct.unpack('<%dI'%(len(d)//4),d);\
	print('/* generated from shaders/fma.comp; regenerate with make shader */\nstatic const uint32_t fma_spv[] = {\n    '+\
	',\n    '.join(', '.join('0x%08x'%x for x in w[i:i+8]) for i in range(0,len(w),8))+'\n};')" > src/fma_spv.h
	rm -f fma.spv

clean:
	rm -f socsonde

.PHONY: shader clean install
