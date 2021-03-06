# do not run directly, use make.sh

CFLAGS_1 =
!ifndef DEBUG
DEBUG = -d0
DSUFFIX =
!else
DSUFFIX = d
!endif

!ifndef CPULEV0
CPULEV0 = 3
!endif
!ifndef CPULEV2
CPULEV2 = 3
!endif
!ifndef CPULEV3
CPULEV3 = 3
!endif
!ifndef CPULEV4
CPULEV4 = 4
!endif
!ifndef CPULEV5
CPULEV5 = 5
!endif
!ifndef CPULEV6
CPULEV6 = 6
!endif
!ifndef TARGET86
TARGET86 = 86
!endif

!ifeq TARGET86 86
TARGET86_1DIGIT=0
!endif
!ifeq TARGET86 186
TARGET86_1DIGIT=1
!endif
!ifeq TARGET86 286
TARGET86_1DIGIT=2
!endif
!ifeq TARGET86 386
TARGET86_1DIGIT=3
!endif
!ifeq TARGET86 486
TARGET86_1DIGIT=4
!endif
!ifeq TARGET86 586
TARGET86_1DIGIT=5
!endif
!ifeq TARGET86 686
TARGET86_1DIGIT=6
!endif

# Include the 2.x headers
OS2_INCLUDE=-i="$(%WATCOM)/h/os2"

TARGET_MSDOS = 32
TARGET_OS2 = 20
SUBDIR   = os2d$(TARGET86_1DIGIT)$(MMODE)$(DSUFFIX)
RC       = wrc
CC       = wcc386
LINKER   = wcl386
WLINK_SYSTEM = os2v2_pm
WLINK_CON_SYSTEM = os2v2
WLINK_DLL_SYSTEM = os2v2_dll

# GUI versions
RCFLAGS  = -q -r -31 -bt=os2v2_pm $(OS2_INCLUDE)
CFLAGS   = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2_pm -oilrtfm -wx -$(CPULEV0) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
CFLAGS386= -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2_pm -oilrtfm -wx -$(CPULEV3) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
CFLAGS386_TO_586= -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2_pm -oilrtfm -wx -fp$(CPULEV5) -$(CPULEV5) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
CFLAGS386_TO_686= -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2_pm -oilrtfm -wx -fp$(CPULEV6) -$(CPULEV6) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
AFLAGS   = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2_pm -wx -$(CPULEV0) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
NASMFLAGS= -DTARGET_MSDOS=32 -DTARGET_OS2=$(TARGET_OS2) -DMSDOS=1 -DTARGET86=$(TARGET86) -DMMODE=$(MMODE)
WLINK_FLAGS = op start=_cstart_

# console versions
RCFLAGS_CON  = -q -r -31 -bt=os2v2 $(OS2_INCLUDE)
CFLAGS_CON   = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -oilrtfm -wx -$(CPULEV0) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
CFLAGS386_CON= -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -oilrtfm -wx -$(CPULEV3) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
CFLAGS386_TO_586_CON= -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -oilrtfm -wx -fp$(CPULEV5) -$(CPULEV5) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
CFLAGS386_TO_686_CON= -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -oilrtfm -wx -fp$(CPULEV6) -$(CPULEV6) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
AFLAGS_CON   = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -wx -$(CPULEV0) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bg
NASMFLAGS_CON= -DTARGET_MSDOS=32 -DTARGET_OS2=$(TARGET_OS2) -DMSDOS=1 -DTARGET86=$(TARGET86) -DMMODE=$(MMODE)
WLINK_CON_FLAGS = op start=_cstart_ 

RCFLAGS_DLL = -q -r -31 -bt=os2v2_dll $(OS2_INCLUDE)
CFLAGS_DLL = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -oilrtfm -wx -$(CPULEV0) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bd
CFLAGS386_DLL = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -oilrtfm -wx -$(CPULEV3) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bd
CFLAGS386_TO_586_DLL = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -oilrtfm -wx -fp$(CPULEV5) -$(CPULEV5) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bd
CFLAGS386_TO_686_DLL = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -oilrtfm -wx -fp$(CPULEV6) -$(CPULEV6) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bd
AFLAGS_DLL = -e=2 -zq -zw -m$(MMODE) $(DEBUG) $(CFLAGS_1) -bt=os2v2 -wx -$(CPULEV0) -dTARGET_MSDOS=32 -dTARGET_OS2=$(TARGET_OS2) -dMSDOS=1 -dTARGET86=$(TARGET86) -DMMODE=$(MMODE) -q $(OS2_INCLUDE) -D_OS2_32_ -bd
NASMFLAGS_DLL = -DTARGET_MSDOS=32 -DTARGET_OS2=$(TARGET_OS2) -DMSDOS=1 -DTARGET86=$(TARGET86) -DMMODE=$(MMODE)

!include "$(REL)$(HPS)mak$(HPS)bcommon.mak"
!include "common.mak"
!include "$(REL)$(HPS)mak$(HPS)dcommon.mak"

