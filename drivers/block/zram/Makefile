# SPDX-License-Identifier: GPL-2.0-only

zram-y	:=	zcomp.o zram_drv.o

zram-$(CONFIG_ZRAM_BACKEND_LZO)		+= backend_lzorle.o backend_lzo.o
zram-$(CONFIG_ZRAM_BACKEND_LZ4)		+= backend_lz4.o
zram-$(CONFIG_ZRAM_BACKEND_LZ4HC)	+= backend_lz4hc.o
zram-$(CONFIG_ZRAM_BACKEND_ZSTD)	+= backend_zstd.o
zram-$(CONFIG_ZRAM_BACKEND_DEFLATE)	+= backend_deflate.o
zram-$(CONFIG_ZRAM_BACKEND_842)		+= backend_842.o
zram-$(CONFIG_ZRAM_ANDROID_IOCTL)	+= zram_ioctl.o

obj-$(CONFIG_ZRAM)	+=	zram.o

zram-$(CONFIG_ZRAM_MEIZU_SMARTSWAP) += smartswap/smartmain.o smartswap/smartswapd.o smartswap/smartswap.o
