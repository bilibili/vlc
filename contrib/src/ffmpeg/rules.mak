# FFmpeg

HASH=v9.8

#FFMPEG_SNAPURL := http://git.videolan.org/?p=ffmpeg.git;a=snapshot;h=$(HASH);sf=tgz
FFMPEG_SNAPURL := http://git.libav.org/?p=libav.git;a=snapshot;h=$(HASH);sf=tgz

FFMPEGCONF = \
	--cc="$(CC)" \
	--disable-doc \
	--enable-runtime-cpudetect \
	--disable-network \
	--disable-protocols \
	--disable-decoder=libvpx \
	--disable-decoder=bink \
	--disable-encoders \
	--disable-libgsm \
	--disable-libopenjpeg \
	--disable-decoders \
	--enable-decoder=aac \
	--enable-decoder=aac_latm \
	--enable-decoder=aasc \
	--enable-decoder=ac3 \
	--enable-decoder=amrnb \
	--enable-decoder=amrwb \
	--enable-decoder=flv \
	--enable-decoder=h263 \
	--enable-decoder=h263i \
	--enable-decoder=h264 \
	--enable-decoder=mp3 \
	--enable-decoder=mp3adu \
	--enable-decoder=mp3adufloat \
	--enable-decoder=mp3float \
	--enable-decoder=mp3on4 \
	--enable-decoder=mp3on4float \
	--enable-decoder=mpeg1video \
	--enable-decoder=mpeg2video \
	--enable-decoder=mpeg4 \
	--enable-decoder=mpeg4v1 \
	--enable-decoder=mpeg4v2 \
	--enable-decoder=mpeg4v3 \
	--enable-decoder=vp3 \
	--enable-decoder=vp5 \
	--enable-decoder=vp6 \
	--enable-decoder=vp6a \
	--enable-decoder=vp6f \
	--enable-decoder=vp8 \
	--enable-decoder=vc1 \
	--enable-decoder=vc1_vdpau \
	--enable-decoder=vc1image \
	--disable-debug \
	--disable-avdevice \
	--disable-devices \
	--disable-avfilter \
	--disable-filters \
	--disable-bsfs \
	--disable-bzlib

# Those tools are named differently in FFmpeg and Libav
#	--disable-ffserver \
#	--disable-ffplay \
#	--disable-ffprobe
DEPS_ffmpeg = zlib

# Optional dependencies
ifdef BUILD_ENCODERS
FFMPEGCONF += --enable-libmp3lame --enable-libvpx --disable-decoder=libvpx --disable-decoder=libvpx_vp8 --disable-decoder=libvpx_vp9
DEPS_ffmpeg += lame $(DEPS_lame) vpx $(DEPS_vpx)
else
FFMPEGCONF += --disable-encoders --disable-muxers
endif

# Small size
ifdef ENABLE_SMALL
FFMPEGCONF += --enable-small
ifeq ($(ARCH),arm)
ifdef HAVE_ARMV7A
FFMPEGCONF += --enable-thumb
endif
endif
endif

ifdef HAVE_CROSS_COMPILE
FFMPEGCONF += --enable-cross-compile
ifndef HAVE_DARWIN_OS
FFMPEGCONF += --cross-prefix=$(HOST)-
endif
endif

# ARM stuff
ifeq ($(ARCH),arm)
FFMPEGCONF += --arch=arm
ifdef HAVE_NEON
FFMPEGCONF += --enable-neon
endif
ifdef HAVE_ARMV7A
FFMPEGCONF += --cpu=cortex-a8
endif
endif

# MIPS stuff
ifeq ($(ARCH),mipsel)
FFMPEGCONF += --arch=mips
endif

# x86 stuff
ifeq ($(ARCH),i386)
ifndef HAVE_DARWIN_OS
FFMPEGCONF += --arch=x86
endif
endif

# Darwin
ifdef HAVE_DARWIN_OS
FFMPEGCONF += --arch=$(ARCH) --target-os=darwin
ifneq ($(findstring $(ARCH),i386 x86_64),)
FFMPEGCONF += --enable-memalign-hack
endif
ifeq ($(ARCH),x86_64)
FFMPEGCONF += --cpu=core2
endif
endif
ifdef HAVE_IOS
ifeq ($(ARCH),arm)
FFMPEGCONF += --enable-pic --as="$(AS)"
endif
endif
ifdef HAVE_MACOSX
FFMPEGCONF += --enable-vda
endif

# Linux
ifdef HAVE_LINUX
FFMPEGCONF += --target-os=linux --enable-pic
endif

# Windows
ifdef HAVE_WIN32
ifndef HAVE_MINGW_W64
DEPS_ffmpeg += directx
endif
FFMPEGCONF += --target-os=mingw32 --enable-memalign-hack
FFMPEGCONF += --enable-w32threads --enable-dxva2 \
	--disable-decoder=dca

ifdef HAVE_WIN64
FFMPEGCONF += --cpu=athlon64 --arch=x86_64
else # !WIN64
FFMPEGCONF+= --cpu=i686 --arch=x86
endif

else # !Windows
FFMPEGCONF += --enable-pthreads
endif

# Disable mpegvideo-based hwaccel - known broken
FFMPEGCONF += \
	$(foreach codec,h263 mpeg1 mpeg2 mpeg4 vc1 wmv3,\
		$(foreach api,dxva2 vaapi vdpau,\
			--disable-hwaccel=$(codec)_$(api)))

# Build
PKGS += ffmpeg
ifeq ($(call need_pkg,"libavcodec >= 54.25.0 libavformat >= 53.21.0 libswscale"),)
PKGS_FOUND += ffmpeg
endif

$(TARBALLS)/ffmpeg-$(HASH).tar.gz:
	$(call download,$(FFMPEG_SNAPURL))

.sum-ffmpeg: $(TARBALLS)/ffmpeg-$(HASH).tar.gz
	$(warning Not implemented.)
	touch $@

ffmpeg: ffmpeg-$(HASH).tar.gz .sum-ffmpeg
	rm -Rf $@ $@-$(HASH)
	mkdir -p $@-$(HASH)
	$(ZCAT) "$<" | (cd $@-$(HASH) && tar xv --strip-components=1)
	$(MOVE)

.ffmpeg: ffmpeg
	cd $< && $(HOSTVARS) ./configure \
		--extra-ldflags="$(LDFLAGS)" $(FFMPEGCONF) \
		--prefix="$(PREFIX)" --enable-static --disable-shared
	cd $< && $(MAKE) install-libs install-headers
	touch $@
