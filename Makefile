UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
PLATFORM=linux
else ifeq ($(UNAME), Darwin)
PLATFORM=osx
else
PLATFORM=windows
endif

all: ubuntu

dev-build:
	scons platform=$(PLATFORM) target=template_debug dev_build=yes debug_symbols=yes compiledb=true #asan=true

format:
	clang-format -i src/*.cpp src/*.h src/host/*.cpp
	gdformat $(shell find -name '*.gd' ! -path './godot-cpp/*' ! -path './modules/godot/*')

UNAME := $(shell uname)
ifeq ($(UNAME), Windows)
    UID=1000
    GID=1000
else
    UID=`id -u`
    GID=`id -g`
endif

SHELL_COMMAND = bash

docker-ubuntu:
	docker build -t godot-lv2host-ubuntu ./platform/ubuntu

shell-ubuntu: docker-ubuntu
	docker run -it --rm -v ${CURDIR}:${CURDIR} --user ${UID}:${GID} -w ${CURDIR} godot-lv2host-ubuntu ${SHELL_COMMAND}

ubuntu:
	$(MAKE) shell-ubuntu SHELL_COMMAND='./platform/ubuntu/build_debug.sh'
	$(MAKE) shell-ubuntu SHELL_COMMAND='./platform/ubuntu/build_release.sh'

docker-mingw:
	docker build -t godot-lv2host-mingw ./platform/mingw

shell-mingw: docker-mingw
	docker run -it --rm -v ${CURDIR}:${CURDIR} --user ${UID}:${GID} -w ${CURDIR} godot-lv2host-mingw ${SHELL_COMMAND}

mingw:
	$(MAKE) shell-mingw SHELL_COMMAND='./platform/mingw/build_debug.sh'
	$(MAKE) shell-mingw SHELL_COMMAND='./platform/mingw/build_release.sh'

clean:
	rm -rf addons/lv2-host/bin modules/godot/bin vcpkg_installed
