SHELL := /bin/bash

ifneq ($(notdir $(CXX)), em++)
$(error You need to install/source emscripten and run with "emmake make")
endif

CXXFLAGS = -std=c++1z \
	--bind \
	--memory-init-file 0 \
	-s EXPORT_NAME=\"'GMICLib'\" \
	-s MODULARIZE=1 \
	-s NO_EXIT_RUNTIME=1 \
	-s TOTAL_MEMORY=$$((2 << 29)) \
	-Dcimg_display=0 \
	-Dcimg_use_zlib=0

CPPFLAGS = -I"./build/include"

LDFLAGS =

LDLIBS =

.PHONY: all release debug clean

all: release

release: CXXFLAGS += -O3 --llvm-lto 1 -s AGGRESSIVE_VARIABLE_ELIMINATION=1 -s OUTLINING_LIMIT=10000 # TODO --closure 1
release: build/gmic.js build/cimg.js

debug: build/gmic.js build/cimg.js

build/cimg.js: bindings.cpp
	em++ $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) bindings.cpp -o build/cimg.js $(LDLIBS)
	echo "self.GMIC = GMICLib(self.GMIC || {});" >> build/cimg.js

#build/gmic.js: CXXFLAGS += -s DISABLE_EXCEPTION_CATCHING=0 # TODO gmic isn't very usable if we don't enable exceptions
build/gmic.js: CXXFLAGS += -Dgmicjs_use_gmic
build/gmic.js: LDFLAGS = -L"./build/lib"
build/gmic.js: LDLIBS = -lgmic -lz
build/gmic.js: bindings.cpp build/lib/libz.a build/lib/libgmic.a
	em++ $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) bindings.cpp -o build/gmic.js $(LDLIBS)
	echo "self.GMIC = GMICLib(self.GMIC || {});" >> build/gmic.js

# zlib is needed because otherwise you get an error: "Could not decompress G'MIC standard library, ignoring it."
build/lib/libz.a:
	set -eux && \
	if [ ! -f "${EMSCRIPTEN}/tests/zlib/build/libz.a" ]; then \
		pushd ${EMSCRIPTEN}/tests/zlib/ && \
		rm -rf build && \
		mkdir build && \
		cd build/ && \
		rm -f ../zconf.h && \
		emcmake cmake .. -DBUILD_SHARED_LIBS=OFF && \
		emmake make VERBOSE=1 zlib && \
		popd; \
	fi
	mkdir -p build/lib
	cp -v ${EMSCRIPTEN}/tests/zlib/build/libz.a ./build/lib/
	mkdir -p build/include
	cp -v ${EMSCRIPTEN}/tests/zlib/zlib.h ./build/include/
	cp -v ${EMSCRIPTEN}/tests/zlib/build/zconf.h ./build/include/

# Build gmic with display and file format stuff disabled
build/lib/libgmic.a: external/gmic build/lib/libz.a
	set -eux && \
	pushd build && \
	emcmake cmake ../external/gmic \
		-DBUILD_LIB=OFF \
		-DBUILD_LIB_STATIC=ON \
		-DBUILD_CLI=OFF \
		-DBUILD_PLUGIN=OFF \
		-DBUILD_MAN=OFF \
		-DENABLE_X=OFF \
		-DENABLE_FFMPEG=OFF \
		-DENABLE_FFTW=OFF \
		-DENABLE_GRAPHICSMAGICK=OFF \
		-DENABLE_JPEG=OFF \
		-DENABLE_OPENCV=OFF \
		-DENABLE_OPENEXR=OFF \
		-DENABLE_OPENMP=OFF \
		-DENABLE_PNG=OFF \
		-DENABLE_TIFF=OFF \
		-DENABLE_ZLIB=ON \
		-DZLIB_LIBRARY=./lib/libz.a \
		-DZLIB_INCLUDE_DIR=./include && \
	emmake make VERBOSE=1 && \
	popd
	mkdir -p build/lib
	cp -v build/libgmic.a ./build/lib/
	mkdir -p build/include
	cp -v external/gmic/src/gmic.h ./build/include/
	cp -v external/gmic/src/CImg.h ./build/include/
	cp -v external/gmic_inpaint.h ./build/include/

# Note: had to removed -fno-ipa-sra in CMakeLists
external/gmic:
	git clone https://github.com/dtschump/gmic external/gmic
	cd external/gmic && git checkout v.179	
	sed -i.bak 's/ -fno-ipa-sra//g' external/gmic/CMakeLists.txt

clean:
	rm -f build/*.js
	#rm -rf ${EMSCRIPTEN}/tests/zlib/build
	#rm -rf external/gmic
	#rm -rf build
