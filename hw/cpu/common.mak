# this makefile is included from all the dos*.mak files, do not use directly
# NTS: HPS is either \ (DOS) or / (Linux)

# The build system we have has no effect on NASM (at the moment) and
# all memory models+CPU modes build the same binary. So why build them
# again and again? Build them only in dos86s mode.
!ifndef TARGET_WINDOWS
! ifndef TARGET_OS2
!  ifeq TARGET_MSDOS 16
!   ifeq TARGET86 86
!    ifeq MMODE s
BUILD_NASM_COM = 1
!    endif
!   endif
!  endif
! endif
!endif

CFLAGS_THIS = -fr=nul -fo=$(SUBDIR)$(HPS).obj -i=.. -i..$(HPS)..
AFLAGS_THIS = -fr=nul -fo=$(SUBDIR)$(HPS).obj -i=.. -i..$(HPS)..
NASMFLAGS_THIS = 
NOW_BUILDING = HW_CPU_LIB

# NTS: CPU functions here are to be moved at some point to the cpu library!
OBJS =        $(SUBDIR)$(HPS)cpuasm.obj $(SUBDIR)$(HPS)cpu.obj $(SUBDIR)$(HPS)gdt_enum.obj

# test programs (MS-DOS and Windows)
MMX_EXE =     $(SUBDIR)$(HPS)mmx.exe
SSE_EXE =     $(SUBDIR)$(HPS)sse.exe
TEST_EXE =    $(SUBDIR)$(HPS)test.exe
DISPSN_EXE =  $(SUBDIR)$(HPS)dispsn.exe
SSEOFF_EXE =  $(SUBDIR)$(HPS)sseoff.exe
GDTLIST_EXE = $(SUBDIR)$(HPS)gdtlist.exe
!ifndef WIN386
GDTTAE_EXE =  $(SUBDIR)$(HPS)gdttae.exe
!endif
RDTSC_EXE =   $(SUBDIR)$(HPS)rdtsc.exe

!ifndef TARGET_WINDOWS
! ifndef TARGET_OS2
# test programs (MS-DOS only)
RESET_EXE =   $(SUBDIR)$(HPS)reset.exe
! endif
!endif

!ifdef BUILD_NASM_COM
# MS-DOS COM output
V86_COM =     $(SUBDIR)$(HPS)v86.com
TSS_COM =     $(SUBDIR)$(HPS)tss.com
PROT286_COM = $(SUBDIR)$(HPS)prot286.com
PROT386_COM = $(SUBDIR)$(HPS)prot386.com
TSSRING_COM = $(SUBDIR)$(HPS)tssring.com
V86KERN_COM = $(SUBDIR)$(HPS)v86kern.com
PROTVCPI_COM =$(SUBDIR)$(HPS)protvcpi.com
PROTDPMI_COM =$(SUBDIR)$(HPS)protdpmi.com
V86KERN2_COM =$(SUBDIR)$(HPS)v86kern2.com
ALIGNCHK_COM =$(SUBDIR)$(HPS)alignchk.com
!endif

$(HW_CPU_LIB): $(OBJS)
	wlib -q -b -c $(HW_CPU_LIB) -+$(SUBDIR)$(HPS)cpu.obj -+$(SUBDIR)$(HPS)cpuasm.obj
	wlib -q -b -c $(HW_CPU_LIB) -+$(SUBDIR)$(HPS)gdt_enum.obj

# NTS we have to construct the command line into tmp.cmd because for MS-DOS
# systems all arguments would exceed the pitiful 128 char command line limit
.C.OBJ:
	%write tmp.cmd $(CFLAGS_THIS) $(CFLAGS_CON) $[@
	$(CC) @tmp.cmd

.ASM.OBJ:
	nasm -o $@ -f obj $(NASMFLAGS_CON) $[@

all: lib exe
	
lib: $(HW_CPU_LIB) .symbolic
	
exe: $(GDTTAE_EXE) $(TEST_EXE) $(DISPSN_EXE) $(RESET_EXE) $(MMX_EXE) $(SSE_EXE) $(SSEOFF_EXE) $(PROT286_COM) $(PROT386_COM) $(TSS_COM) $(TSSRING_COM) $(ALIGNCHK_COM) $(V86_COM) $(V86KERN_COM) $(V86KERN2_COM) $(PROTVCPI_COM) $(PROTDPMI_COM) $(RDTSC_EXE) $(GDTLIST_EXE) .symbolic

!ifdef BUILD_NASM_COM
$(V86_COM): v86.asm
	nasm -o $@ -f bin $(NASMFLAGS) $[@

$(TSS_COM): tss.asm
	nasm -o $@ -f bin $(NASMFLAGS) $[@

$(TSSRING_COM): tssring.asm
	nasm -o $@ -f bin $(NASMFLAGS) $[@

$(PROT286_COM): prot286.asm
	nasm -o $@ -f bin $(NASMFLAGS) $[@

$(PROT386_COM): prot386.asm
	nasm -o $@ -f bin $(NASMFLAGS) $[@

$(V86KERN_COM): v86kern.asm
	nasm -o $@ -f bin $(NASMFLAGS) -l $(SUBDIR)$(HPS)v86kern.lst -O9 $[@

$(PROTVCPI_COM): protvcpi.asm
	nasm -o $@ -f bin $(NASMFLAGS) -l $(SUBDIR)$(HPS)protvcpi.lst -O9 $[@

$(PROTDPMI_COM): protdpmi.asm
	nasm -o $@ -f bin $(NASMFLAGS) -l $(SUBDIR)$(HPS)protdpmi.lst -O9 $[@

$(V86KERN2_COM): v86kern2.asm
	nasm -o $@ -f bin $(NASMFLAGS) -l $(SUBDIR)$(HPS)v86kern2.lst -O9 $[@

$(ALIGNCHK_COM): alignchk.asm
	nasm -o $@ -f bin $(NASMFLAGS) $[@
!endif

$(MMX_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(SUBDIR)$(HPS)mmx.obj
	%write tmp.cmd option quiet system $(WLINK_CON_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) file $(SUBDIR)$(HPS)mmx.obj
! ifdef TARGET_WINDOWS
!  ifeq TARGET_MSDOS 16
	%write tmp.cmd segment TYPE CODE PRELOAD FIXED DISCARDABLE SHARED
	%write tmp.cmd segment TYPE DATA PRELOAD MOVEABLE
!  endif
! endif
	%write tmp.cmd name $(MMX_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
! ifdef WIN386
	@$(WIN386_EXE_TO_REX_IF_REX) $(MMX_EXE)
	@wbind $(MMX_EXE) -q -n
! endif
! ifdef WIN_NE_SETVER_BUILD
	$(WIN_NE_SETVER_BUILD) $(MMX_EXE)
! endif

$(SSE_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(SUBDIR)$(HPS)sse.obj
	%write tmp.cmd option quiet system $(WLINK_CON_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) file $(SUBDIR)$(HPS)sse.obj
! ifdef TARGET_WINDOWS
!  ifeq TARGET_MSDOS 16
	%write tmp.cmd segment TYPE CODE PRELOAD FIXED DISCARDABLE SHARED
	%write tmp.cmd segment TYPE DATA PRELOAD MOVEABLE
!  endif
! endif
	%write tmp.cmd name $(SSE_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
! ifdef WIN386
	@$(WIN386_EXE_TO_REX_IF_REX) $(SSE_EXE)
	@wbind $(SSE_EXE) -q -n
! endif
! ifdef WIN_NE_SETVER_BUILD
	$(WIN_NE_SETVER_BUILD) $(SSE_EXE)
! endif

$(TEST_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(SUBDIR)$(HPS)test.obj
	%write tmp.cmd option quiet system $(WLINK_CON_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) file $(SUBDIR)$(HPS)test.obj option map=$(SUBDIR)$(HPS)test.map
! ifdef TARGET_WINDOWS
!  ifeq TARGET_MSDOS 16
	%write tmp.cmd segment TYPE CODE PRELOAD FIXED DISCARDABLE SHARED
	%write tmp.cmd segment TYPE DATA PRELOAD MOVEABLE
!  endif
! endif
	%write tmp.cmd name $(TEST_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
! ifdef WIN386
	@$(WIN386_EXE_TO_REX_IF_REX) $(TEST_EXE)
	@wbind $(TEST_EXE) -q -n
! endif
! ifdef WIN_NE_SETVER_BUILD
	$(WIN_NE_SETVER_BUILD) $(TEST_EXE)
! endif

$(RDTSC_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(HW_8254_LIB) $(SUBDIR)$(HPS)rdtsc.obj
	%write tmp.cmd option quiet system $(WLINK_CON_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) library $(HW_8254_LIB) file $(SUBDIR)$(HPS)rdtsc.obj
! ifdef TARGET_WINDOWS
!  ifeq TARGET_MSDOS 16
	%write tmp.cmd segment TYPE CODE PRELOAD FIXED DISCARDABLE SHARED
	%write tmp.cmd segment TYPE DATA PRELOAD MOVEABLE
!  endif
! endif
	%write tmp.cmd name $(RDTSC_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
! ifdef WIN386
	@$(WIN386_EXE_TO_REX_IF_REX) $(RDTSC_EXE)
	@wbind $(RDTSC_EXE) -q -n
! endif
! ifdef WIN_NE_SETVER_BUILD
	$(WIN_NE_SETVER_BUILD) $(RDTSC_EXE)
! endif

!ifdef RESET_EXE
$(RESET_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(HW_8042_LIB) $(HW_8254_LIB) $(SUBDIR)$(HPS)reset.obj
	%write tmp.cmd option quiet system $(WLINK_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) library $(HW_8042_LIB) library $(HW_8254_LIB) file $(SUBDIR)$(HPS)reset.obj
	%write tmp.cmd name $(RESET_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
!endif

$(DISPSN_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(SUBDIR)$(HPS)dispsn.obj
	%write tmp.cmd option quiet system $(WLINK_CON_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) file $(SUBDIR)$(HPS)dispsn.obj
! ifdef TARGET_WINDOWS
!  ifeq TARGET_MSDOS 16
	%write tmp.cmd segment TYPE CODE PRELOAD FIXED DISCARDABLE SHARED
	%write tmp.cmd segment TYPE DATA PRELOAD MOVEABLE
!  endif
! endif
	%write tmp.cmd name $(DISPSN_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
! ifdef WIN386
	@$(WIN386_EXE_TO_REX_IF_REX) $(DISPSN_EXE)
	@wbind $(DISPSN_EXE) -q -n
! endif
! ifdef WIN_NE_SETVER_BUILD
	$(WIN_NE_SETVER_BUILD) $(DISPSN_EXE)
! endif

$(SSEOFF_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(SUBDIR)$(HPS)sseoff.obj
	%write tmp.cmd option quiet system $(WLINK_CON_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) file $(SUBDIR)$(HPS)sseoff.obj
! ifdef TARGET_WINDOWS
!  ifeq TARGET_MSDOS 16
	%write tmp.cmd segment TYPE CODE PRELOAD FIXED DISCARDABLE SHARED
	%write tmp.cmd segment TYPE DATA PRELOAD MOVEABLE
!  endif
! endif
	%write tmp.cmd name $(SSEOFF_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
! ifdef WIN386
	@$(WIN386_EXE_TO_REX_IF_REX) $(SSEOFF_EXE)
	@wbind $(SSEOFF_EXE) -q -n
! endif
! ifdef WIN_NE_SETVER_BUILD
	$(WIN_NE_SETVER_BUILD) $(SSEOFF_EXE)
! endif

$(GDTLIST_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(SUBDIR)$(HPS)gdtlist.obj
	%write tmp.cmd option quiet system $(WLINK_CON_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) file $(SUBDIR)$(HPS)gdtlist.obj
! ifdef TARGET_WINDOWS
!  ifeq TARGET_MSDOS 16
	%write tmp.cmd segment TYPE CODE PRELOAD FIXED DISCARDABLE SHARED
	%write tmp.cmd segment TYPE DATA PRELOAD MOVEABLE
!  endif
! endif
	%write tmp.cmd name $(GDTLIST_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
! ifdef WIN386
	@$(WIN386_EXE_TO_REX_IF_REX) $(GDTLIST_EXE)
	@wbind $(GDTLIST_EXE) -q -n
! endif
! ifdef WIN_NE_SETVER_BUILD
	$(WIN_NE_SETVER_BUILD) $(GDTLIST_EXE)
! endif

!ifdef GDTTAE_EXE
$(GDTTAE_EXE): $(HW_CPU_LIB) $(HW_CPU_LIB_DEPENDENCIES) $(SUBDIR)$(HPS)gdttae.obj
	%write tmp.cmd option quiet system $(WLINK_CON_SYSTEM) $(WLINK_FLAGS) $(HW_CPU_LIB_WLINK_LIBRARIES) file $(SUBDIR)$(HPS)gdttae.obj
! ifdef TARGET_WINDOWS
!  ifeq TARGET_MSDOS 16
	%write tmp.cmd segment TYPE CODE PRELOAD FIXED DISCARDABLE SHARED
	%write tmp.cmd segment TYPE DATA PRELOAD MOVEABLE
!  endif
! endif
	%write tmp.cmd name $(GDTTAE_EXE)
	@wlink @tmp.cmd
	@$(COPY) ..$(HPS)..$(HPS)dos32a.dat $(SUBDIR)$(HPS)dos4gw.exe
! ifdef WIN386
	@$(WIN386_EXE_TO_REX_IF_REX) $(GDTTAE_EXE)
	@wbind $(GDTTAE_EXE) -q -n
! endif
! ifdef WIN_NE_SETVER_BUILD
	$(WIN_NE_SETVER_BUILD) $(GDTTAE_EXE)
! endif
!endif

clean: .SYMBOLIC
          del $(SUBDIR)$(HPS)*.obj
          del $(HW_CPU_LIB)
          del tmp.cmd
          @echo Cleaning done
