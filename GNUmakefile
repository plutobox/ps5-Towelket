# Windows native build using MSVC Build Tools, Ninja, and a user-local vcpkg.
# Override these when your tools live elsewhere, e.g.
#   make windows VCPKG_ROOT=C:\\tools\\vcpkg
SHELL := cmd.exe

VCPKG_ROOT ?= $(USERPROFILE)\vcpkg
WINDOWS_BUILD_DIR ?= build-windows
VSDEVCMD ?= C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat

.PHONY: windows

windows:
	call "$(VSDEVCMD)" -arch=x64 -host_arch=x64 && cmake -S . -B $(WINDOWS_BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static -DPLAYER_BUILD_LIBLCF=ON && cmake --build $(WINDOWS_BUILD_DIR) --parallel
