# PowerWeb —— 构建脚本（GNU make）
#
# 交叉编译（Linux / macOS，需要 mingw-w64）：
#   make CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres
#
# 本机 MinGW（Windows，gcc 和 windres 已在 PATH）：
#   make
#
# 常用参数：
#   make VERSION=1.2.0            版本号（同时写入 exe 属性和 --version）
#   make dist                     生成可直接运行的 build/dist/
#   make zip                      打出发布压缩包
#   make clean

VERSION  ?= 1.0.0
comma    := ,
VER_NUM  ?= $(subst .,$(comma),$(VERSION))

CC       ?= gcc
WINDRES  ?= windres
PYTHON   ?= python3

CFLAGS   ?= -O2 -Wall -Wextra -std=c99
LDFLAGS  ?= -static -s
LIBS     := -lws2_32

BUILD    := build
OBJ      := $(BUILD)/obj
DIST     := $(BUILD)/dist
TARGET   := $(DIST)/PowerWeb.exe

SRC      := src/powerweb.c
RC       := res/app.rc
RESOBJ   := $(OBJ)/app.res.o
ZIP      := $(BUILD)/PowerWeb-$(VERSION)-win64.zip

# 需要随 exe 一起分发的东西（exe 只在自身目录找 index.html）
DIST_FILES := web/index.html \
              packaging/start-exe.bat \
              packaging/open-firewall.bat \
              packaging/enable-autostart.bat \
              packaging/disable-autostart.bat \
              packaging/install-autostart.ps1

.PHONY: all dist zip clean run help

all: $(TARGET) dist

# 版本号来自这里，C 源码和资源脚本都包含它
$(BUILD)/version.h: Makefile
	@mkdir -p $(BUILD)
	@printf '#ifndef PW_VERSION\n#define PW_VERSION "%s"\n#endif\n#ifndef PW_VER_NUM\n#define PW_VER_NUM %s\n#endif\n' \
		"$(VERSION)" "$(VER_NUM)" > $@

$(OBJ)/powerweb.o: $(SRC) $(BUILD)/version.h
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -I$(BUILD) -c $< -o $@

$(RESOBJ): $(RC) res/app.ico $(BUILD)/version.h
	@mkdir -p $(OBJ)
	$(WINDRES) -I res -I $(BUILD) --output-format=coff -o $@ $<

$(TARGET): $(OBJ)/powerweb.o $(RESOBJ)
	@mkdir -p $(DIST)
	$(CC) $(LDFLAGS) -o $@ $(OBJ)/powerweb.o $(RESOBJ) $(LIBS)

# 把页面和批处理拷到 exe 旁边，得到一个双击即用的目录
dist: $(TARGET)
	@for f in $(DIST_FILES); do cp -f $$f $(DIST)/; done
	@echo "==> ready: $(TARGET)"

zip: dist
	cd $(DIST) && $(PYTHON) -c "import shutil; shutil.make_archive('$(abspath $(BUILD))/PowerWeb-$(VERSION)-win64','zip','.')"
	@echo "==> $(ZIP)"

run: $(TARGET)
	cd $(DIST) && ./PowerWeb.exe

clean:
	rm -rf $(BUILD)/obj $(BUILD)/dist $(BUILD)/version.h $(ZIP)

help:
	@echo "make                build into build/dist/"
	@echo "make VERSION=1.2.0  set version string"
	@echo "make zip            build/PowerWeb-<version>-win64.zip"
	@echo "make clean          remove build output"
