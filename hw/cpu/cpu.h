/* cpu.h
 *
 * Runtime CPU detection library.
 * (C) 2009-2012 Jonathan Campbell.
 * Hackipedia DOS library.
 *
 * This code is licensed under the LGPL.
 * <insert LGPL legal text here>
 *
 */

#ifndef __HW_CPU_CPU_H
#define __HW_CPU_CPU_H

#include <stdint.h>
#include <dos.h>

#if !defined(FAR)
# if defined(TARGET_WINDOWS)
#  include <windows.h>
# else
#  if TARGET_MSDOS == 32
#   define FAR
#  else
#   define FAR far
#  endif
# endif
#endif

/* FIX: Open Watcom does not provide inpd/outpd in 16-bit real mode, so we have to provide it ourself */
/*      We assume for the same stupid reasons the pragma aux function can't be used because it's a 386 level instruction */
#if TARGET_MSDOS == 16
uint32_t __cdecl inpd(uint16_t port);
void __cdecl outpd(uint16_t port,uint32_t data);
#endif

#if TARGET_MSDOS == 16
static inline uint32_t ptr2phys(void far *p) {
	return (((uint32_t)FP_SEG(p)) << 4UL) +
		((uint32_t)FP_OFF(p));
}
#endif

#pragma pack(push,1)
struct cpu_cpuid_features {
	union {
		uint32_t		raw[4];	/* EAX, EBX, EDX, ECX */
		struct {
			uint32_t	todo[4];
		} f;
	} a;
};

struct cpu_cpuid_ext_features {
	union {
		uint32_t		raw[4];	/* EAX, EBX, ECX, EDX */
		struct {
			uint32_t	todo[4];
		} f;
	} a;
};

struct cpu_cpuid_ext_cache_tlb {
	union {
		uint32_t		raw[4];	/* EAX, EBX, ECX, EDX */
		struct {
			uint32_t	todo[4];
		} f;
	} a;
};

struct cpu_cpuid_ext_cache_tlb_l2 {
	union {
		uint32_t		raw[4];	/* EAX, EBX, ECX, EDX */
		struct {
			uint32_t	todo[4];
		} f;
	} a;
};

struct cpu_cpuid_ext_longmode {
	union {
		uint32_t		raw[4];	/* EAX, EBX, ECX, EDX */
		struct {
			uint32_t	todo[4];
		} f;
	} a;
};

struct cpu_cpuid_ext_apm {
	union {
		uint32_t		raw[1]; /* EAX */
		struct {
			uint32_t	todo[1];
		} f;
	} a;
};

struct cpuid_result {
	uint32_t			eax,ebx,ecx,edx;
};

struct cpu_serial_number {
	uint32_t	raw[4];	/* EDX, ECX, EBX, EAX */
};
#pragma pack(pop)

/* "Basic" CPU level */
enum {
	CPU_8086=0,		/* 0 */
	CPU_186,
	CPU_286,
	CPU_386,
	CPU_486,
	CPU_586,
	CPU_686,
	CPU_MAX
};

struct cpu_cpuid_ext_info {
	char					brand[49];
	uint32_t				cpuid_max;
	struct cpu_cpuid_ext_features		features;
	struct cpu_cpuid_ext_cache_tlb		cache_tlb;
	struct cpu_cpuid_ext_cache_tlb_l2	cache_tlb_l2;
	struct cpu_cpuid_ext_apm		apm;
	struct cpu_cpuid_ext_longmode		longmode;
};

extern unsigned char			cpu_sse_usable;
extern unsigned char			cpu_sse_usable_probed;
extern const char*			cpu_sse_unusable_reason;
extern unsigned char			cpu_sse_usable_can_turn_on;
extern unsigned char			cpu_sse_usable_can_turn_off;
extern const char*			cpu_sse_usable_detection_method;

extern struct cpu_cpuid_ext_info*	cpu_cpuid_ext_info;
extern const char *			cpu_basic_level_str[CPU_MAX];
extern char				cpu_cpuid_vendor[13];
extern struct cpu_cpuid_features	cpu_cpuid_features;
extern signed char			cpu_basic_level;
extern uint32_t				cpu_cpuid_max;
extern unsigned char			cpu_flags;
extern uint16_t				cpu_tmp1;

/* compatability */
#define cpu_v86_active (cpu_flags & CPU_FLAG_V86_ACTIVE)

/* has L1 TLB/CACHE info */
#define cpu_cpuid_ext_info_has_cache_tlb_l1 (cpu_cpuid_ext_info && cpu_cpuid_ext_info->cpuid_max >= 0x80000005UL)

/* has L2 TLB/CACHE info */
#define cpu_cpuid_ext_info_has_cache_tlb_l2 (cpu_cpuid_ext_info && cpu_cpuid_ext_info->cpuid_max >= 0x80000006UL)

/* has APM info */
#define cpu_cpuid_ext_info_has_apm (cpu_cpuid_ext_info && cpu_cpuid_ext_info->cpuid_max >= 0x80000007UL)

/* has Long mode info */
#define cpu_cpuid_ext_info_has_longmode (cpu_cpuid_ext_info && cpu_cpuid_ext_info->cpuid_max >= 0x80000008UL)

#define cpu_basic_level_to_string(x) (x >= 0 ? cpu_basic_level_str[x] : "?")

/* CPU flag: CPU supports CPUID */
#define CPU_FLAG_CPUID			(1 << 0)
#define CPU_FLAG_FPU			(1 << 1)
#define CPU_FLAG_CPUID_EXT		(1 << 2)
#define CPU_FLAG_V86_ACTIVE		(1 << 3)
#define CPU_FLAG_PROTECTED_MODE		(1 << 4)
#define CPU_FLAG_PROTECTED_MODE_32	(1 << 5)
/*      ^ Windows-specific: we are not only a 16-bit Win16 app, but Windows is running in 386 enhanced mode
 *                          and we can safely use 32-bit registers and hacks. This will always be set for
 *                          Win32 and 32-bit DOS, obviously. If set, PROTECTED_MODE is also set. */
#define CPU_FLAG_DONT_WRITE_RDTSC	(1 << 6)

void cpu_probe();
int cpu_basic_probe(); /* external assembly language function */
int cpu_extended_cpuid_probe();
void cpu_extended_cpuid_info_free();

/* WARNING: Caller is expected to check CPUID information to ensure the
            processor supports this feature! */
void cpu_ask_serial();
void cpu_disable_serial();
extern struct cpu_serial_number cpu_serial;

static void _cli();
#pragma aux _cli = "cli"
static void _sti();
#pragma aux _sti = "sti"

/* NTS: remember for Watcom: 16-bit realmode sizeof(int) == 2, 32-bit flat mode sizeof(int) == 4 */
static unsigned int get_cpu_flags();
#if TARGET_MSDOS == 32
#pragma aux get_cpu_flags = \
	"pushfd"	\
	"pop	eax"	\
	value [eax];
#else
#pragma aux get_cpu_flags = \
	"pushf"		\
	"pop	ax"	\
	value [ax];
#endif

static void set_cpu_flags(unsigned int f);
#if TARGET_MSDOS == 32
#pragma aux set_cpu_flags = \
	"push	eax"	\
	"popfd	"	\
	parm [eax];
#else
#pragma aux set_cpu_flags = \
	"push	ax"	\
	"popf	"	\
	parm [ax];
#endif

#if defined(TARGET_WINDOWS) && TARGET_MSDOS == 32
/* Watcom does not offer int86/int386 for Win32s/Win9x/NT/etc */
#else
static inline void just_int86(unsigned char c,union REGS *r1,union REGS *r2) {
# ifdef __386__
	int386(c,r1,r2);
# else
	int86(c,r1,r2);
# endif
}
#endif

#if TARGET_MSDOS == 32
static inline void cpu_cpuid(uint32_t idx,struct cpuid_result *x);
#pragma aux cpu_cpuid = \
	".586p" \
	"xor ebx,ebx" \
	"mov ecx,ebx" \
	"mov edx,ebx" \
	"cpuid" \
	"mov [esi],eax" \
	"mov [esi+4],ebx" \
	"mov [esi+8],ecx" \
	"mov [esi+12],edx" \
	parm [eax] [esi] \
	modify [ebx ecx edx]
#else
void cpu_cpuid(uint32_t idx,struct cpuid_result *x);
#endif

int cpu_check_sse_is_usable();
int cpu_sse_disable();
int cpu_sse_enable();

#endif /* __HW_CPU_CPU_H */