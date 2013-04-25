/* FIXME: This just in: your IRQ testing routine can hang the system especially (as in DOSBox) when IRQs that are
 *        normally masked have a corresponding interrupt vector set to NULL (0000:0000). You need to double-check
 *        the probe function and make sure there is no possible way uninitialized vectors execute when any
 *        particular IRQ is fired. */

/* FIXME: There might be a way to deal with some troubles you've been having with this code:
 *
 *        Troubles:
 *          - Gravis SBOS/MEGA-EM NMI causes DOS4GW.EXE to hard-reset the machine rather than deal with it
 *          - Even when your code caught the NMI and forwarded it to Gravis's TSRs, Sound Blaster emulation
 *            failed to signal your interrupt handler, which is probably why 16-bit real mode builds worked
 *            fine with SBOS/MEGA-EM while 32-bit builds did not.
 *          - Under DOS4GW.EXE (but not DOS32A.EXE) using IRQ 8-15 eventually resulted in missing an IRQ.
 *            I initially panned it as DOS4GW.EXE being a cheap son-of-a-bitch but it might be related instead
 *            to a combination of the BIOS forwarding interrupts in real mode, and the lack of a real-mode
 *            interrupt handler for the Sound Blaster library.
 *
 *        Things to do:
 *          - Add code to set the interrupt vector for the IRQ on behalf of the calling program. The calling
 *            program provides a function to call, and you set the vector (and keep track of the previous one).
 *          - Also add function to remove interrupt vector. Write the code so that if someone else hooked,
 *            the unhook function returns an error.
 *          - Add code to interrupt vector hooking code so that in 32-bit builds, if MEGA-EM or SBOS is resident
 *            or DOS4GW.EXE is resident and IRQ 8-15 is to be used, this code automatically installs a real-mode
 *            interrupt vector that reflects the interrupt into protected mode.
 *          - Add code so that, in 32-bit builds, if MEGA-EM or SBOS is resident, the library hooks the NMI
 *            interrupt in protected mode and reflects it to real-mode so Gravis's emulation can work. [FIXME
 *            can you also resolve DOS4GW.EXE hard-crashing in this situation?] */

/* Primary Sound Blaster support library.
 * This compiles into sndsb.lib.
 * This DOES NOT include support for Plug & Play versions,
 * functions for that are in sndsbpnp.c -> sndsbpnp.lib */

#include <stdio.h>
#include <conio.h> /* this is where Open Watcom hides the outp() etc. functions */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>
#include <fcntl.h>
#include <dos.h>

#include <hw/dos/dos.h>
#include <hw/dos/dosbox.h>
#include <hw/8237/8237.h>		/* 8237 DMA */
#include <hw/8254/8254.h>		/* 8254 timer */
#include <hw/8259/8259.h>		/* 8259 PIC interrupts */
#include <hw/sndsb/sndsb.h>

/* Windows 9x VxD enumeration */
#include <windows/w9xvmm/vxd_enum.h>

/* uncomment this to enable debugging messages */
//#define DBG

#if defined(DBG)
# define DEBUG(x) (x)
#else
# define DEBUG(x)
#endif

static int			adpcm_pred = 128;
static signed char		adpcm_last = 0;
static unsigned char		adpcm_step = 0;
static unsigned char		adpcm_error = 0;
static unsigned char		adpcm_lim = 0;

/* CT1335 */
struct sndsb_mixer_control sndsb_mixer_ct1335[] = {
/*      index, of,ln,name */
	{0x02, 1, 3, "Master"},
	{0x06, 1, 3, "MIDI"},
	{0x08, 1, 3, "CD"},
	{0x0A, 1, 2, "Voice"}
};

/* CT1345 */
struct sndsb_mixer_control sndsb_mixer_ct1345[] = {
/*      index, of,ln,name */
	{0x04, 1, 3, "Voice R"},		{0x04, 5, 3, "Voice L"},
	{0x0A, 1, 2, "Mic"},
	{0x0C, 1, 2, "Input source"},		{0x0C, 3, 1, "Lowpass filter"},		{0x0C, 5, 1, "Input filter"},
	{0x0E, 1, 1, "Stereo switch"},		{0x0E, 5, 1, "Output filter"},
	{0x22, 1, 3, "Master R"},		{0x22, 5, 3, "Master L"},
	{0x26, 1, 3, "MIDI R"},			{0x26, 5, 3, "MIDI L"},
	{0x28, 1, 3, "CD R"},			{0x28, 5, 3, "CD L"},
	{0x2E, 1, 3, "Line R"},			{0x2E, 5, 3, "Line L"}
};

/* CT1745 */
struct sndsb_mixer_control sndsb_mixer_ct1745[] = {
/*      index, of,ln,name */
	{0x30, 3, 5, "Master L"},		{0x31, 3, 5, "Master R"},
	{0x32, 3, 5, "Volume L"},		{0x33, 3, 5, "Volume R"},
	{0x34, 3, 5, "MIDI L"},			{0x35, 3, 5, "MIDI R"},
	{0x36, 3, 5, "CD L"},			{0x37, 3, 5, "CD R"},
	{0x38, 3, 5, "Line L"},			{0x39, 3, 5, "Line R"},
	{0x3A, 3, 5, "Mic"},
	{0x3B, 6, 2, "PC speaker"},

	{0x3C, 0, 1, "Out mixer: mic"},
	{0x3C, 1, 1, "Out mixer: CD R"},
	{0x3C, 2, 1, "Out mixer: CD L"},
	{0x3C, 3, 1, "Out mixer: Line R"},
	{0x3C, 4, 1, "Out mixer: Line L"},

	{0x3D, 0, 1, "In mixer L: Mic"},
	{0x3D, 1, 1, "In mixer L: CD R"},
	{0x3D, 2, 1, "In mixer L: CD L"},
	{0x3D, 3, 1, "In mixer L: Line R"},
	{0x3D, 4, 1, "In mixer L: Line L"},
	{0x3D, 5, 1, "In mixer L: MIDI R"},
	{0x3D, 6, 1, "In mixer L: MIDI L"},

	{0x3E, 0, 1, "In mixer R: Mic"},
	{0x3E, 1, 1, "In mixer R: CD R"},
	{0x3E, 2, 1, "In mixer R: CD L"},
	{0x3E, 3, 1, "In mixer R: Line R"},
	{0x3E, 4, 1, "In mixer R: Line L"},
	{0x3E, 5, 1, "In mixer R: MIDI R"},
	{0x3E, 6, 1, "In mixer R: MIDI L"},

	{0x3F, 6, 2, "Input gain L"},
	{0x40, 6, 2, "Input gain R"},
	{0x41, 6, 2, "Output gain L"},
	{0x42, 6, 2, "Output gain R"},
	{0x43, 0, 1, "AGC"},
	{0x44, 4, 4, "Treble L"},
	{0x45, 4, 4, "Treble R"},
	{0x46, 4, 4, "Bass L"},
	{0x47, 4, 4, "Bass R"}
};

signed char gallant_sc6600_map_to_dma[4] = {-1, 0, 1, 3};
signed char gallant_sc6600_map_to_irq[8] = {-1, 7, 9,10,11, 5,-1,-1};

const char* sndsb_adpcm_mode_str[4] = {
	"none",
	"ADPCM 4-bit",
	"ADPCM 2.6-bit",
	"ADPCM 2-bit"
};

const char *sndsb_mixer_chip_name[SNDSB_MIXER_MAX] = {
	"none",
	"CT1335",
	"CT1345",
	"CT1745"
};

const char *sndsb_dspoutmethod_str[SNDSB_DSPOUTMETHOD_MAX] = {
	"direct",
	"1.xx",
	"2.00",
	"2.01",
	"3.xx",
	"4.xx"
};

#if TARGET_MSDOS == 32
signed char			sndsb_nmi_32_hook = -1;
#endif

struct sndsb_probe_opts sndsb_probe_options={0};
struct sndsb_ctx sndsb_card[SNDSB_MAX_CARDS];
struct sndsb_ctx *sndsb_card_blaster=NULL;
int sndsb_card_next = 0;

struct sndsb_ctx *sndsb_by_base(uint16_t x) {
	int i;

	for (i=0;i < SNDSB_MAX_CARDS;i++) {
		if (sndsb_card[i].baseio == x)
			return sndsb_card+i;
	}

	return 0;
}

struct sndsb_ctx *sndsb_by_mpu(uint16_t x) {
	int i;

	for (i=0;i < SNDSB_MAX_CARDS;i++) {
		if (sndsb_card[i].mpuio == x)
			return sndsb_card+i;
	}

	return 0;
}

struct sndsb_ctx *sndsb_by_irq(int8_t x) {
	int i;

	for (i=0;i < SNDSB_MAX_CARDS;i++) {
		if (sndsb_card[i].irq == x)
			return sndsb_card+i;
	}

	return 0;
}

struct sndsb_ctx *sndsb_by_dma(uint16_t x) {
	int i;

	for (i=0;i < SNDSB_MAX_CARDS;i++) {
		if (sndsb_card[i].baseio > 0 && (sndsb_card[i].dma8 == x || sndsb_card[i].dma16 == x))
			return sndsb_card+i;
	}

	return 0;
}

#if TARGET_MSDOS == 32
int sb_nmi_32_auto_choose_hook() {
	if (sndsb_nmi_32_hook >= 0)
		return sndsb_nmi_32_hook;

	/* auto-detect SBOS/MEGA-EM and enable nmi reflection if present */
	if (gravis_mega_em_detect(&megaem_info) || gravis_sbos_detect() >= 0)
		return 1;

	return 0;
}
#endif

void free_sndsb() {
#if TARGET_MSDOS == 32
	if (sndsb_nmi_32_hook) do_nmi_32_unhook();
#endif
}

int init_sndsb() {
	memset(sndsb_card,0,sizeof(sndsb_card));
	sndsb_card_blaster = NULL;
	sndsb_card_next = 0;
#if TARGET_MSDOS == 32
	sndsb_nmi_32_hook = sb_nmi_32_auto_choose_hook();
	if (sndsb_nmi_32_hook) do_nmi_32_hook();
#endif
	return 1;
}

struct sndsb_ctx *sndsb_alloc_card() {
	int i;

	for (i=0;i < SNDSB_MAX_CARDS;i++) {
		if (sndsb_card[i].baseio == 0 && sndsb_card[i].mpuio == 0)
			return sndsb_card+i;
	}

	return NULL;
}

void sndsb_free_card(struct sndsb_ctx *c) {
	memset(c,0,sizeof(*c));
	if (c == sndsb_card_blaster) sndsb_card_blaster = NULL;
}

struct sndsb_ctx *sndsb_try_blaster_var() {
	int A=-1,I=-1,D=-1,H=-1,P=-1;
	struct sndsb_ctx *e;
	char *s;

	if (sndsb_card_blaster != NULL)
		return sndsb_card_blaster;

	s = getenv("BLASTER");
	if (s == NULL) return NULL;

	while (*s != 0) {
		if (*s == ' ') {
			s++;
			continue;
		}

		if (*s == 'A') {
			s++;
			A = strtol(s,&s,16);
		}
		else if (*s == 'P') {
			s++;
			P = strtol(s,&s,16);
		}
		else if (*s == 'I') {
			s++;
			I = strtol(s,&s,10);
		}
		else if (*s == 'D') {
			s++;
			D = strtol(s,&s,10);
		}
		else if (*s == 'H') {
			s++;
			H = strtol(s,&s,10);
		}
		else {
			while (*s && *s != ' ') s++;
			while (*s == ' ') s++;
		}
	}

	if (A < 0 || I < 0 || D < 0 || I > 15 || D > 7)
		return NULL;

	if (sndsb_by_base(A) != NULL)
		return 0;

	e = sndsb_alloc_card();
	if (e == NULL) return NULL;
	e->is_gallant_sc6600 = 0;
	e->baseio = (uint16_t)A;
	e->mpuio = (uint16_t)(P > 0 ? P : 0);
	e->dma8 = (int8_t)D;
	e->dma16 = (int8_t)H;
	e->irq = (int8_t)I;
	e->dsp_vmaj = 0;
	e->dsp_vmin = 0;
	e->mixer_ok = 0;
	e->mpu_ok = 0;
	e->dsp_ok = 0;
	return (sndsb_card_blaster=e);
}

const char *sndsb_mixer_chip_str(uint8_t c) {
	if (c >= SNDSB_MIXER_MAX) return NULL;
	return sndsb_mixer_chip_name[c];
}

/* NTS: this function enforces a timeout of 250ms */
int sndsb_read_dsp(struct sndsb_ctx *cx) {
	unsigned int patience = 25000;
	int c = -1;

	do {
		if (inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS) & 0x80) { /* data available? */
			c = inp(cx->baseio+SNDSB_BIO_DSP_READ_DATA);
			break;
		}

		t8254_wait(t8254_us2ticks(10));
		if (--patience == 0) {
			DEBUG(fprintf(stdout,"sndsb_read_dsp() read timeout\n"));
			return -1;
		}
	} while (1);

	DEBUG(fprintf(stdout,"sndsb_read_dsp() == 0x%02X\n",c));
	return c;
}

int sndsb_reset_dsp(struct sndsb_ctx *cx) {
	if (cx->baseio == 0) {
		DEBUG(fprintf(stdout,"BUG: sndsb baseio == 0\n"));
		return 0;
	}

	/* DSP reset procedure */
	/* "write 1 to the DSP and wait 3 microseconds" */
	DEBUG(fprintf(stdout,"sndsb_reset_dsp() reset in progress\n"));
	outp(cx->baseio+SNDSB_BIO_DSP_RESET,1);
	t8254_wait(t8254_us2ticks(1000));	/* be safe and wait 1ms */
	outp(cx->baseio+SNDSB_BIO_DSP_RESET,0);

	/* wait for the DSP to return 0xAA */
	/* "typically the DSP takes about 100us to initialize itself" */
	if (sndsb_read_dsp(cx) != 0xAA) {
		if (sndsb_read_dsp(cx) != 0xAA) {
			DEBUG(fprintf(stdout,"sndsb_read_dsp() did not return satisfactory answer\n"));
			return 0;
		}
	}

	return 1;
}

int sndsb_read_mixer(struct sndsb_ctx *cx,uint8_t i) {
	outp(cx->baseio+SNDSB_BIO_MIXER_INDEX,i);
	return inp(cx->baseio+SNDSB_BIO_MIXER_DATA);
}

void sndsb_write_mixer(struct sndsb_ctx *cx,uint8_t i,uint8_t d) {
	outp(cx->baseio+SNDSB_BIO_MIXER_INDEX,i);
	outp(cx->baseio+SNDSB_BIO_MIXER_DATA,d);
}

unsigned char sndsb_test_write_mixer(struct sndsb_ctx *cx,uint8_t i,uint8_t d) {
	unsigned char o,c;
	o = sndsb_read_mixer(cx,i); sndsb_write_mixer(cx,i,d);
	c = sndsb_read_mixer(cx,i); sndsb_write_mixer(cx,i,o);
	return c;
}

/* NTS: If DOSBox's emulation is correct, 0xFF is not necessarily what is returned for
 *      unknown registers, therefore it's not an accurate way to probe the chipset.
 *      DOSBox for example seems to return 0x0A. */
int sndsb_probe_mixer(struct sndsb_ctx *cx) {
	cx->mixer_chip = 0;
	/* if there is a wider "master volume" control 0x30 then we're a CT1745 or higher */
	if (	(sndsb_test_write_mixer(cx,0x30,0x08)&0xF8) == 0x08 &&
		(sndsb_test_write_mixer(cx,0x30,0x38)&0xF8) == 0x38 &&
		(sndsb_test_write_mixer(cx,0x31,0x08)&0xF8) == 0x08 &&
		(sndsb_test_write_mixer(cx,0x31,0x38)&0xF8) == 0x38) {
		cx->mixer_chip = SNDSB_MIXER_CT1745;
	}
	/* If there is a "master volume" control at 0x22 then we're at CT1345 or higher */
	else if((sndsb_test_write_mixer(cx,0x22,0x33)&0xEE) == 0x22 &&
		(sndsb_test_write_mixer(cx,0x22,0xCC)&0xEE) == 0xCC) {
		cx->mixer_chip = SNDSB_MIXER_CT1345;
	}
	/* hm, may be at CT1335 */
	else if((sndsb_test_write_mixer(cx,0x02,0x02)&0x0E) == 0x02 &&
		(sndsb_test_write_mixer(cx,0x02,0x0C)&0x0E) == 0x0C) {
		cx->mixer_chip = SNDSB_MIXER_CT1335;
	}

	sndsb_choose_mixer(cx,-1);
	return 1;
}

int sndsb_mpu_command(struct sndsb_ctx *cx,uint8_t d) {
	unsigned int patience = 100;

	do {
		if (inp(cx->mpuio+SNDSB_MPUIO_STATUS) & 0x40) /* if not ready for cmd, wait and try again */
			t8254_wait(t8254_us2ticks(100));
		else {
			outp(cx->mpuio+SNDSB_MPUIO_COMMAND,d);
			return 1;
		}
	} while (--patience != 0);
	return 0;
}

int sndsb_mpu_write(struct sndsb_ctx *cx,uint8_t d) {
	unsigned int patience = 100;

	do {
		if (inp(cx->mpuio+SNDSB_MPUIO_STATUS) & 0x40) /* if not ready for cmd, wait and try again */
			t8254_wait(t8254_us2ticks(100));
		else {
			outp(cx->mpuio+SNDSB_MPUIO_DATA,d);
			return 1;
		}
	} while (--patience != 0);
	return 0;
}

int sndsb_mpu_read(struct sndsb_ctx *cx) {
	unsigned int patience = 100;

	do {
		if (inp(cx->mpuio+SNDSB_MPUIO_STATUS) & 0x80) /* if data ready not ready, wait and try again */
			t8254_wait(t8254_us2ticks(100));
		else {
			return inp(cx->mpuio+SNDSB_MPUIO_DATA);
		}
	} while (--patience != 0);

	return -1;
}

/* this code makes sure the MPU exists */
int sndsb_probe_mpu401(struct sndsb_ctx *cx) {
	unsigned int patience = 10;
	int c;

	if (cx->mpuio == 0) return 0;

	/* check the command register. note however that if neither data is available
	 * or a command can be written this can return 0xFF */
	if (inp(cx->mpuio+SNDSB_MPUIO_STATUS) == 0xFF) {
		/* hm, perhaps it's stuck returning data? */
		do { /* wait for it to signal no data and/or ability to write command */
			inp(cx->mpuio+SNDSB_MPUIO_DATA);
			if (inp(cx->mpuio+SNDSB_MPUIO_STATUS) != 0xFF)
				break;

			if (--patience == 0) return 0;
			t8254_wait(t8254_us2ticks(100)); /* 100us */
		} while(1);
	}

	patience=3;
	do {
		/* OK we got the status register to return something other than 0xFF.
		 * Issue a reset */
		if (sndsb_mpu_command(cx,0xFF)) {
			if ((c=sndsb_mpu_read(cx)) == 0xFE) {
				break;
			}
		}

		if (--patience == 0)
			return 0;

		t8254_wait(t8254_us2ticks(10)); /* 10us */
	} while (1);

	return 1;
}

int sndsb_write_dsp(struct sndsb_ctx *cx,uint8_t d) {
	unsigned int patience = 25000;

	DEBUG(fprintf(stdout,"sndsb_write_dsp(0x%02X)\n",d));
	do {
		if (inp(cx->baseio+SNDSB_BIO_DSP_WRITE_STATUS) & 0x80)
			t8254_wait(t8254_us2ticks(10));
		else {
			outp(cx->baseio+SNDSB_BIO_DSP_WRITE_DATA,d);
			return 1;
		}
	} while (--patience != 0);
	DEBUG(fprintf(stdout,"sndsb_write_dsp() timeout\n"));
	return 0;
}

int sndsb_write_dsp_timeconst(struct sndsb_ctx *cx,uint8_t tc) {
	if (!sndsb_write_dsp(cx,0x40))
		return 0;
	if (!sndsb_write_dsp(cx,tc))
		return 0;
	return 1;
}

int sndsb_query_dsp_version(struct sndsb_ctx *cx) {
	int a,b;

	if (!sndsb_write_dsp(cx,SNDSB_DSPCMD_GET_VERSION))
		return 0;

	if ((a=sndsb_read_dsp(cx)) < 0)
		return 0;
	if ((b=sndsb_read_dsp(cx)) < 0)
		return 0;
	if (a == 0xFF || b == 0xFF)
		return 0;

	cx->dsp_vmaj = (uint8_t)a;
	cx->dsp_vmin = (uint8_t)b;
	DEBUG(fprintf(stdout,"sndsb_query_dsp_version() == v%u.%u\n",a,b));
	return 1;
}

/* NTS: We do not test IRQ and DMA channels here */
/* NTS: The caller may have set irq == -1, dma8 == -1, or dma16 == -1, such as
 *      when probing. If any of them are -1, and this code knows how to deduce
 *      it directly from the hardware, then they will be updated */
int sndsb_init_card(struct sndsb_ctx *cx) {
	cx->windows_creative_sb16_drivers_ver = 0;
	cx->windows_creative_sb16_drivers = 0;
	cx->windows_9x_me_sbemul_sys = 0;
	cx->dsp_play_flipped_sign = 0;
	cx->buffer_irq_interval = 0;
	cx->windows_springwait = 0;
	cx->is_gallant_sc6600 = 0;
	cx->windows_emulation = 0;
	cx->windows_xp_ntvdm = 0;
	cx->dsp_copyright[0] = 0;
	cx->dsp_1xx_autoinit = 1;
	cx->buffer_last_io = 0;
	cx->direct_dsp_io = 0;
	cx->goldplay_mode = 0;
	cx->vdmsound = 0;
	cx->mega_em = 0;
	cx->sbos = 0;
	cx->mpu_ok = 0;
	cx->dsp_ok = 0;
	cx->mixer_ok = 0;
	cx->dsp_vmaj = 0;
	cx->dsp_vmin = 0;
	cx->buffer_phys = 0;
	cx->buffer_rate = 22050;
	cx->enable_adpcm_autoinit = 0;
	cx->dsp_adpcm = 0;
	cx->dsp_record = 0;
	cx->sb_mixer_items = 0;
	cx->sb_mixer = NULL;

	if (!sndsb_reset_dsp(cx)) return 0;
	cx->dsp_ok = 1;

	/* hm, what version are you? */
	sndsb_query_dsp_version(cx);

	/* FIX: Apparently when SBOS unloads it leaves behind the I/O
	        port stuck returning 0xAA, which can trick most SB
		compatible DOS programs into thinking the DSP is still
		there. But if we read back the DSP version as 0xAA 0xAA
		then we know it's just SBOS not cleaning up after itself */
	if (cx->dsp_vmaj == 0xAA && cx->dsp_vmin == 0xAA)
		return 0; /* That's not a Sound Blaster! */

	/* FIX: Gravis Ultrasound MEGA-EM emulates a VERY LIMITED SUBSET
	        of the Sound Blaster. Worse, it hangs the machine with an
		error message when you use any command it doesn't know!

		MEGA-EM usually reports itself as v1.3, but you can also
		use EMUSET -X2 to enable a rudimentary sort of DSP v2.1
		emulation. True SB hardware doesn't carry a DSP copyright
		string until v3.0, so it's probable we wouldn't get anything
		anyway. The DSP copyright string command is not recognized
		by MEGA-EM. */
	if (cx->dsp_vmaj <= 2) {
		if (gravis_mega_em_detect(&megaem_info)) { /* is that you, MEGA-EM? */
			/* FIXME: Is there some sort of hack we can use
			          to determine what I/O port MEGA-EM is
				  watching? I would like this code to
				  not self-limit it's capabilities to
				  ALL interfaces just because one happens
				  to be a GUS with MEGA-EM. */
			cx->mega_em = 1;
			cx->dsp_1xx_autoinit = 0;
			strcpy(cx->dsp_copyright,"Gravis MEGA-EM");
		}
	}

	/* if the DSP is recent enough, it might have a copyright string.

	   DSPs prior to the Sound Blaster Pro don't really seem to return
	   anything in response to DSP command 0xE3, in fact it might be that
	   Creative added it with the SB16 (DSP v4.x). However I also know
	   from programming experience that many SB clones DO return a string
	   and report themselves with varying version numbers like v3.1, v2.5,
	   etc. */
	if (!cx->mega_em && cx->dsp_vmaj >= 2) {
		int i,c;

		sndsb_write_dsp(cx,0xE3);
		for (i=0;i < (sizeof(cx->dsp_copyright)-1);i++) {
			c = sndsb_read_dsp(cx);
			if (c < 0) break;
			cx->dsp_copyright[i] = (char)c;
			if (c == 0) break;
		}
		cx->dsp_copyright[i] = (char)0;
		DEBUG(fprintf(stdout,"sndsb_init_card() copyright == '%s'\n",cx->dsp_copyright));

		/* check for whacked-out DSP "strings" like a continuous
		   sequence of 0x01 bytes. it might be Gravis SBOS, which
		   also reports itself as DSP v2.1 */
		if (cx->dsp_vmaj <= 2 && i >= (sizeof(cx->dsp_copyright)-1)) {
			for (i=0,c=0;i < (sizeof(cx->dsp_copyright)-1);i++) {
				if (cx->dsp_copyright[i] == 1)
					c++;
				else
					break;
			}

			if (c == i) {
				if (gravis_sbos_detect() >= 0) { /* is that you, SBOS? */
					strcpy(cx->dsp_copyright,"Gravis SBOS");
					cx->dsp_1xx_autoinit = 0;
					cx->sbos = 1;
				}
			}
		}
	}

	if (sndsb_probe_mixer(cx)) /* NTS: Don't reset the mixer, just probe it */
		cx->mixer_ok = 1;

	/* Sound Blaster 16 (DSP 4.xx): we read the mixer registers */
	/* Earlier cards: we have to probe around for it */
	if (cx->dsp_vmaj == 4 && !sndsb_probe_options.disable_sb16_read_config_byte) {
		unsigned char irqm = sndsb_read_mixer(cx,0x80);
		unsigned char dmam = sndsb_read_mixer(cx,0x81);
		if (cx->irq < 0 && irqm != 0xFF && irqm != 0x00) {
			if (irqm & 8)		cx->irq = 10;
			else if (irqm & 4)	cx->irq = 7;
			else if (irqm & 2)	cx->irq = 5;
			else if (irqm & 1)	cx->irq = 2;
		}
		if (dmam != 0xFF && dmam != 0x00) {
			if (cx->dma8 < 0) {
				if (dmam & 8)		cx->dma8 = 3;
				else if (dmam & 2)	cx->dma8 = 1;
				else if (dmam & 1)	cx->dma8 = 0;
			}

			if (cx->dma16 < 0) {
				if (dmam & 0x80)	cx->dma16 = 7;
				else if (dmam & 0x40)	cx->dma16 = 6;
				else if (dmam & 0x20)	cx->dma16 = 5;
			}

			if (cx->dma16 == -1)
				cx->dma16 = cx->dma8;
		}
	}

	/* Reveal SC400 SB16 clone: I have this card and I can tell
	 * from programming experience that while it reports itself
	 * as DSP v3.5 (Sound Blaster Pro) it actually has a SB16
	 * type mixer and supports most (but not all) of the SB16
	 * type DSP commands. It lacks however the configuration
	 * registers in the mixer for DMA and IRQ settings, on
	 * this card you use secret undocumented DSP commands.
	 * The card also has a "Windows Sound System" interface
	 * at 0x530, which is not relevent here since we focus on
	 * the Sound Blaster part.
	 *
	 * It also has a amusing hardware bug where I can set the
	 * DSP up as if doing a DMA transfer, and then my code can
	 * fake the DMA transfer by writing to the DSP command
	 * port, something I took advantage of prior to figuring out
	 * the DMA controller back in the day */
	if (!strcmp(cx->dsp_copyright,"SC-6000")) {
		if (sndsb_write_dsp(cx,0x58)) {
			unsigned char a,b,c;
			a = (unsigned char)sndsb_read_dsp(cx);
			if (a == 0xAA) a = (unsigned char)sndsb_read_dsp(cx);
			/* observation: if the card is jumpered to 220h, the first byte is 0x2E, second is 0xC5.
			                if the card is jumpered to 240h, the first byte is 0x2F, second is 0xC3.
					is that really what bit 0 indicates? */
			if ((a&0xFE) == 0x2E) {
				cx->is_gallant_sc6600 = 1;
				b = (unsigned char)sndsb_read_dsp(cx);
				c = (unsigned char)sndsb_read_dsp(cx);
				if (b != 0 && c != 0) {
					/* SC400: Experience says the card always works over
					 * the 8-bit DMA even for 16-bit PCM audio */
					if (cx->dma8 < 0)
						cx->dma8 = gallant_sc6600_map_to_dma[c&3];
					if (cx->dma16 < 0)
						cx->dma16 = cx->dma8;
					if (cx->irq < 0)
						cx->irq = gallant_sc6600_map_to_irq[(c>>3)&7];
				}
			}
		}
	}

#if TARGET_MSDOS == 16 && defined(__COMPACT__)
	/* trimmed to keep total code <= 64KB */
#else
	if (windows_mode == WINDOWS_NT) {
		/* Windows NT would never let a DOS program like us talk directly to hardware,
		 * so if we see a Sound Blaster like device it's very likely an emulation driver.
		 * Working correctly then requires us to deduce what emulation driver we're running under. */

		/* No copyright string and DSP v2.1: Microsoft Windows XP/Vista/7 native NTVDM.EXE SB emulation */
		if (cx->dsp_copyright[0] == 0 && cx->dsp_vmaj == 2 && cx->dsp_vmin == 1) {
			cx->windows_xp_ntvdm = 1;
			cx->windows_emulation = 1;
			strcpy(cx->dsp_copyright,"Microsoft Windows XP/Vista/7 NTVDM.EXE SB emulation");
		}
		/* anything else: probably VDMSOUND.EXE, which provides good enough emulation we don't need workarounds */
		/* TODO: Is there anything we can do to detect with certainty that it's VDMSOUND.EXE? */
		else {
			/* NTS: VDMSOUND.EXE emulates everything down to the copyright string, so append to the string instead of replacing */
			char *x = cx->dsp_copyright+strlen(cx->dsp_copyright);
			char *f = cx->dsp_copyright+sizeof(cx->dsp_copyright)-1;
			const char *add = " [VDMSOUND]";

			cx->vdmsound = 1;
			cx->windows_emulation = 1;
			if (x != cx->dsp_copyright) {
				while (x < f && *add) *x++ = *add++;
				*x = 0;
			}
			else {
				strcpy(cx->dsp_copyright,"VDMSOUND");
			}
		}
	}
	else if (windows_mode == WINDOWS_ENHANCED) { /* Windows 9x/ME Sound Blaster */
		struct w9x_vmm_DDB_scan vxdscan;
		unsigned char vxdscanned = 0;

		/* Two possibilities from experience:
		 *    a) We can see and talk to the Sound Blaster because the driver
		 *       implements a "pass through" virtualization like mode. Meaning,
		 *       if we start talking to the Sound Blaster the driver catches it
		 *       and treats it as yet another process opening the sound card.
		 *       Meaning that, as long as we have the sound card playing audio,
		 *       other Windows applications cannot open it, and if other Windows
		 *       applications have it open, we cannot initialize the card.
		 *
		 *       That scenario is typical of:
		 *
		 *          - Microsoft Windows 95 with Microsoft stock Sound Blaster 16 drivers
		 *
		 *       That scenario brings up an interesting problem as well: Since
		 *       it literally "passes through" the I/O, we see the card for what
		 *       it is. So we can't check for inconsistencies in I/O like we can
		 *       with most emulations. If knowing about this matters enough, we
		 *       have to know how to poke around inside the Windows kernel and
		 *       autodetect which drivers are resident.
		 *
		 *    b) The Sound Blaster is virtual. In the most common case with
		 *       Windows 9x, it likely means there's a PCI-based sound card
		 *       installed with kernel-level emulation enabled. What we are able
		 *       to do depends on what the driver offers us.
		 *
		 *       That scenario is typical of:
		 *
		 *          Microsoft Windows 98/ME with PCI-based sound hardware. In
		 *          one test scenario of mine, it was a Sound Blaster Live!
		 *          value card and Creative "Sound Blaster 16 emulation" drivers.
		 *
		 *          Microsoft Windows ME, through SBEMUL.SYS, which uses the
		 *          systemwide default sound driver to completely virtualize
		 *          the sound card. On one virtual machine, Windows ME uses the
		 *          AC'97 codec driver to emulate a Sound Blaster Pro.
		 *
		 *       Since emulation can vary greatly, detecting the emulator through
		 *       I/O inconsistencies is unlikely to work, again, if it matters we
		 *       need a way to poke into the Windows kernel and look at which drivers
		 *       are resident.
		 *
		 *    c) The Sound Blaster is actual hardware, and Windows is not blocking
		 *       or virtualizing any I/O ports.
		 *
		 *       I know you're probably saying to yourself: Ick! But under Windows 95
		 *       such scenarios are possible: if there is a SB16 compatible sound
		 *       card out there and no drivers are installed to talk to it, Windows
		 *       95 itself will not talk to the card, but will allow DOS programs
		 *       to do so. Amazingly, it will still virtualize the DMA controller
		 *       in a manner that allows everything to work!
		 *
		 *       Whatever you do in this scenario: Don't let multiple DOS boxes talk
		 *       to the same Sound Blaster card!!!
		 *
		 * So unlike the Windows NT case, we can't assume emulators or capabilities
		 * because it varies greatly depending on the host configuration. But we
		 * can try our best and at least try to avoid things that might trip up
		 * Windows 9x. */
		cx->windows_emulation = 1; /* NTS: The "pass thru" scenario counts as emulation */

		if (!sndsb_probe_options.disable_windows_vxd_checks && w9x_vmm_first_vxd(&vxdscan)) {
			vxdscanned = 1;
			do {
				/* If SBEMUL.SYS is present, then it's definitely Windows 98/ME SBEMUL.SYS */
				if (!memcmp(vxdscan.ddb.name,"SBEMUL  ",8)) {
					cx->windows_9x_me_sbemul_sys = 1;
				}
				/* If a Sound Blaster 16 is present, usually Windows 9x/ME will install the
				 * stock Creative drivers shipped on the CD-ROM */
				else if (!memcmp(vxdscan.ddb.name,"VSB16   ",8)) {
					cx->windows_creative_sb16_drivers = 1;
					cx->windows_creative_sb16_drivers_ver = ((uint16_t)vxdscan.ddb.Mver << 8U) | ((uint16_t)vxdscan.ddb.minorver);
				}
			} while (w9x_vmm_next_vxd(&vxdscan));
			w9x_vmm_last_vxd(&vxdscan);
		}

		/* DSP v3.2, no copyright string, and (no VxD scan OR SBEMUL.SYS is visible)
		 * Might be Microsoft Windows 98/ME SBEMUL.SYS */
		if ((!vxdscanned || cx->windows_9x_me_sbemul_sys) && cx->dsp_vmaj == 3 && cx->dsp_vmin == 2 && cx->dsp_copyright[0] == 0) {
			/* No hacks required, the emulation is actually quite reasonable, though as usual
			 * using extremely short block sizes will cause stuttering. The only recognition
			 * necessary is that SBEMUL.SYS does not support the ADPCM modes. Applies to Windows 98
			 * and Windows ME. */
			strcpy(cx->dsp_copyright,"Microsoft Windows 98/ME SBEMUL.SYS");
		}
		/* Sound Blaster 16 DSP, and VSB16 (SB16.VXD) is visible.
		 * Might be Creative's Sound Blaster 16 drivers. */
		else if (cx->windows_creative_sb16_drivers && cx->dsp_vmaj == 4 && !strcmp(cx->dsp_copyright,"COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.")) {
			sprintf(cx->dsp_copyright,"Creative Sound Blaster 16 driver for Win 3.1/9x/ME v%u.%u",
				cx->windows_creative_sb16_drivers_ver>>8,
				cx->windows_creative_sb16_drivers_ver&0xFF);
		}
	}

	/* add our commentary for other emulation environments we can detect */
	if (detect_dosbox_emu() && !cx->windows_emulation) {
		/* add commentary if we know we're running under the DOSBox emulator.
		 * Nothing special, DOSBox does a damn good job at it's emulation so
		 * no workarounds are required. */
		char *x = cx->dsp_copyright+strlen(cx->dsp_copyright);
		char *f = cx->dsp_copyright+sizeof(cx->dsp_copyright)-1;
		const char *add = " [DOSBox]";

		if (x != cx->dsp_copyright) {
			while (x < f && *add) *x++ = *add++;
			*x = 0;
		}
		else {
			strcpy(cx->dsp_copyright,"DOSBox emulator");
		}
	}
#endif

	if (cx->mpuio == 0) { /* uh oh, we have to probe for it */
		if (sndsb_by_mpu(0x330) == NULL) {
			cx->mpuio = 0x330; /* more common */
			if (sndsb_probe_mpu401(cx))
				cx->mpu_ok = 1;
			else {
				if (sndsb_by_mpu(0x300) == NULL) {
					cx->mpuio = 0x300; /* less common */
					if (sndsb_probe_mpu401(cx))
						cx->mpu_ok = 1;
					else {
						cx->mpuio = 0;
					}
				}
			}
		}
	}
	else {
		if (sndsb_probe_mpu401(cx))
			cx->mpu_ok = 1;
	}

	/* if any of our tests left the SB IRQ hanging, clear it now */
	if (cx->irq >= 0) {
		sndsb_interrupt_ack(cx,3);
		sndsb_interrupt_ack(cx,3);
	}

	sndsb_determine_ideal_dsp_play_method(cx);
	return 1;
}

int sndsb_determine_ideal_dsp_play_method(struct sndsb_ctx *cx) {
	if (cx->irq < 0 || cx->dma8 < 0) /* No IRQ, no DMA, fallback to direct */
		cx->dsp_play_method = SNDSB_DSPOUTMETHOD_DIRECT;
	else if (cx->dsp_vmaj >= 4 || cx->is_gallant_sc6600)
		cx->dsp_play_method = SNDSB_DSPOUTMETHOD_4xx;
	else if (cx->dsp_vmaj == 3)
		cx->dsp_play_method = SNDSB_DSPOUTMETHOD_3xx;
	else if (cx->dsp_vmaj == 2 && cx->dsp_vmin >= 1) {
		/* Gravis SBOS does not do auto-init at all.
		   Gravis MEGA-EM will fucking hang the computer and gripe
		   about "unknown DSP command 1Ch" despite reporting itself
		   as DSP v2.1 (EMUSET -X2). So don't do it! */
		if (cx->sbos || cx->mega_em)
			cx->dsp_play_method = SNDSB_DSPOUTMETHOD_1xx;
		else
			cx->dsp_play_method = SNDSB_DSPOUTMETHOD_201;
	}
	else if (cx->dsp_vmaj == 2)
		cx->dsp_play_method = SNDSB_DSPOUTMETHOD_200;
	else if (cx->dsp_vmaj == 1)
		cx->dsp_play_method = SNDSB_DSPOUTMETHOD_1xx;
	else
		cx->dsp_play_method = SNDSB_DSPOUTMETHOD_DIRECT;

	return 1;
}

#if TARGET_MSDOS == 16 && defined(__COMPACT__)
#else
static unsigned char sb_test_irq_number = 0;
static volatile unsigned short int sb_test_irq_flag = 0;
static void interrupt far sb_test_irq() {
	sb_test_irq_flag++;
	if (sb_test_irq_number >= 8) p8259_OCW2(8,P8259_OCW2_NON_SPECIFIC_EOI);
	p8259_OCW2(0,P8259_OCW2_NON_SPECIFIC_EOI);
}
#endif

/* alternative "lite" IRQ probing that hooks the interrupt and wait for an event.
 * Microsoft Windows friendly version that avoids 1) PIC commands to read back
 * events and 2) The undocumented DSP command 0xF2 that triggers an interrupt.
 *
 * While the primary method in manual_probe_irq() works well in pure DOS and
 * some DOS boxes, this lite version works better in virtualized environments
 * like Windows NT/9x DOS boxes. */
void sndsb_alt_lite_probe_irq(struct sndsb_ctx *cx) {
#if TARGET_MSDOS == 16 && defined(__COMPACT__)
	/* too much code */
#else
	void (interrupt *old_irq)() = NULL;
	unsigned int round = 0,tolerance;
	unsigned char ml,mh,maybe;
	unsigned int patience = 0;
	unsigned short eliminated = 0U,possible;
	unsigned char tries[] = {5,7},tri;
	unsigned int testlen = 22050/20; /* 1/20th of a second */
	struct dma_8237_allocation *dma;
	const unsigned char timeconst = (unsigned char)((65536UL - (256000000UL / 22050UL)) >> 8UL);
	DEBUG(fprintf(stdout,"Sound blaster IRQ unknown, I'm going to have to probe for it [alt lite]\n"));

	/* for this test we initiate playback of short blocks. so we must ensure that this
	 * card has a known DMA channel assignment. */
	if (cx->dma8 < 0) return;

	dma = dma_8237_alloc_buffer(testlen);
	if (dma == NULL) return;

#if TARGET_MSDOS == 32
	memset(dma->lin,128,testlen);
#else
	_fmemset(dma->lin,128,testlen);
#endif

	/* save the IRQ mask */
	_cli();
	ml = p8259_read_mask(0);	/* IRQ0-7 */
	mh = p8259_read_mask(8);	/* IRQ8-15 */
	
	round = 0;
	do {
		if (++round >= 8)
			break;

		possible = 0;
		/* go through the remaining ones, one at a time */
		for (tri=0;tri < sizeof(tries);tri++) {
			if (eliminated & (1U << tries[tri]))
				continue;
			if (!sndsb_reset_dsp(cx)) {
				DEBUG(fprintf(stdout,"WARNING: DSP reset failed, aborting IRQ probe\n"));
				break;
			}

			DEBUG(fprintf(stdout,"  Now testing IRQ %u\n",tries[tri]));

			/* clear SoundBlaster's previous interrupt */
			inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS);

			p8259_mask(tries[tri]);
			/* hook the interrupt, reset the flag, unmask the interrupt */
			sb_test_irq_flag = 0;
			sb_test_irq_number = tries[tri];
			old_irq = _dos_getvect(irq2int(tries[tri]));
			_dos_setvect(irq2int(tries[tri]),sb_test_irq);
			p8259_unmask(tries[tri]);

			/* wait for IRQ to show response (prior to triggering one) */
			_sti();
			maybe = 0;
			patience = 140;
			tolerance = 0;
			do {
				if (sb_test_irq_flag) {
					_cli();
					/* VDMSOUND bugfix: a previous invocation of this program without playing sound
					 * leaves the IRQ primed and ready to trigger the instant this code tests again,
					 * leading to false "Caught IRQ prior to DSP command" situations. It's sort of
					 * like the "stuck IRQ" situation and Gravis Ultrasound cards. */
					if (tri == 0 && tolerance == 0) {
						sb_test_irq_flag = 0;
						tolerance++;
						patience = 250;
						_sti();
					}
					else {
						break;
					}
				}
				t8254_wait(t8254_us2ticks(1000));
			} while (--patience != 0);

			/* if the IRQ triggered between unmasking and NOW, then clearly it doesn't belong to the SB */
			if (sb_test_irq_flag) {
				eliminated |= 1U << tries[tri];
				DEBUG(fprintf(stdout,"Caught IRQ prior to DSP command, updating IRQ elimination: 0x%04x\n",eliminated));
				p8259_mask(tries[tri]);
				_dos_setvect(irq2int(tries[tri]),old_irq);
				continue;
			}

			/* make the SoundBlaster trigger an interrupt by playing a short sample block */
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(cx->dma8) | D8237_MASK_SET); /* mask */
			outp(D8237_REG_W_WRITE_MODE,
				D8237_MODER_CHANNEL(cx->dma8) |
				D8237_MODER_TRANSFER(D8237_MODER_XFER_READ) |
				D8237_MODER_MODESEL(D8237_MODER_MODESEL_SINGLE));
			d8237_write_base(cx->dma8,dma->phys); /* RAM location with not much around */
			d8237_write_count(cx->dma8,testlen);
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(cx->dma8)); /* unmask */

			/* Time Constant */
			if (!sndsb_write_dsp_timeconst(cx,timeconst) || !sndsb_write_dsp(cx,0x14) ||
				!sndsb_write_dsp(cx,testlen-1) || !sndsb_write_dsp(cx,(testlen-1)>>8)) {
				outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(cx->dma8) | D8237_MASK_SET); /* unmask */
				p8259_mask(tries[tri]);
				_dos_setvect(irq2int(tries[tri]),old_irq);
				continue;
			}

			/* wait for IRQ to show response */
			_sti();
			maybe = 0;
			patience = 140;
			do {
				if (sb_test_irq_flag) {
					DEBUG(fprintf(stdout,"Flag with %ums to go for IRQ %d\n",patience,tries[tri]));
					_cli();
					sb_test_irq_flag = 0; /* immediately clear it */
					maybe = 1;
					break;
				}
				t8254_wait(t8254_us2ticks(1000));
			} while (--patience != 0);
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(cx->dma8) | D8237_MASK_SET); /* unmask */

			DEBUG(fprintf(stdout,"    maybe=%u\n",maybe));
			if (maybe == 0) {
				p8259_mask(tries[tri]);
				_dos_setvect(irq2int(tries[tri]),old_irq);
				continue;
			}

			if (!sndsb_reset_dsp(cx)) {
				DEBUG(fprintf(stdout,"WARNING: DSP reset failed, aborting IRQ probe\n"));
				p8259_mask(tries[tri]);
				_dos_setvect(irq2int(tries[tri]),old_irq);
				break;
			}

			/* wait for IRQ to show response (prior to triggering one) */
			_cli();
			sb_test_irq_flag = 0;
			_sti();
			maybe = 0;
			patience = 140;
			do {
				if (sb_test_irq_flag) break;
				t8254_wait(t8254_us2ticks(1000));
			} while (--patience != 0);

			/* if the IRQ triggered between unmasking and NOW, then clearly it doesn't belong to the SB */
			if (sb_test_irq_flag) {
				eliminated |= 1U << tries[tri];
				DEBUG(fprintf(stdout,"Caught IRQ prior to DSP command, updating IRQ elimination: 0x%04x\n",eliminated));
				p8259_mask(tries[tri]);
				_dos_setvect(irq2int(tries[tri]),old_irq);
				continue;
			}

			/* make the SoundBlaster trigger an interrupt by playing a short sample block */
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(cx->dma8) | D8237_MASK_SET); /* mask */
			outp(D8237_REG_W_WRITE_MODE,
				D8237_MODER_CHANNEL(cx->dma8) |
				D8237_MODER_TRANSFER(D8237_MODER_XFER_READ) |
				D8237_MODER_MODESEL(D8237_MODER_MODESEL_SINGLE));
			d8237_write_base(cx->dma8,dma->phys); /* RAM location with not much around */
			d8237_write_count(cx->dma8,testlen);
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(cx->dma8)); /* unmask */

			/* Time Constant */
			if (!sndsb_write_dsp_timeconst(cx,timeconst) || !sndsb_write_dsp(cx,0x14) ||
				!sndsb_write_dsp(cx,testlen-1) || !sndsb_write_dsp(cx,(testlen-1)>>8)) {
				outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(cx->dma8) | D8237_MASK_SET); /* unmask */
				p8259_mask(tries[tri]);
				_dos_setvect(irq2int(tries[tri]),old_irq);
				continue;
			}

			/* wait for IRQ to show response */
			_sti();
			maybe = 0;
			patience = 140;
			do {
				if (sb_test_irq_flag) {
					DEBUG(fprintf(stdout,"Flag with %ums to go on IRQ %d\n",patience,tries[tri]));
					_cli();
					sb_test_irq_flag = 0; /* immediately clear it */
					maybe = 1;
					break;
				}
				t8254_wait(t8254_us2ticks(1000));
			} while (--patience != 0);
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(cx->dma8) | D8237_MASK_SET); /* unmask */

			DEBUG(fprintf(stdout,"    maybe2=%u\n",maybe));
			if (maybe == 0) {
				p8259_mask(tries[tri]);
				_dos_setvect(irq2int(tries[tri]),old_irq);
				continue;
			}

			if (!sndsb_reset_dsp(cx)) {
				DEBUG(fprintf(stdout,"WARNING: DSP reset failed, aborting IRQ probe\n"));
				p8259_mask(tries[tri]);
				_dos_setvect(irq2int(tries[tri]),old_irq);
				break;
			}

			/* OK cleanup */
			p8259_mask(tries[tri]);
			_dos_setvect(irq2int(tries[tri]),old_irq);

			possible |= 1U << tries[tri];
			DEBUG(fprintf(stdout,"Possible=0x%04X\n",possible));
		}
		/* loop while we see possibilities, but more than one IRQ appears to be it */
		DEBUG(fprintf(stdout,"Round %u result: possible=0x%04x\n",possible));
	} while (possible != 0 && (possible&(possible-1)) != 0);

	if (possible != 0 && (possible&(possible-1)) == 0) {
		for (tri=0;tri < sizeof(tries);tri++) {
			if (possible & (1U << tries[tri])) {
				cx->irq = tries[tri];
				break;
			}
		}
	}

	/* release DMA buffer */
	dma_8237_free_buffer(dma);

	/* restore interrupt mask */
	_cli();
	p8259_write_mask(0,ml);
	p8259_write_mask(8,mh);
	_sti();
#endif
}

/* On Sound Blaster cards prior to the SB16 the only way to autodetect the IRQ
 * was to cause a SB IRQ and watch the interrupt controller to see which one
 * went off. that's what this function does. */
/* NTS: This doesn't work in some situations:
 *        - Windows XP native Sound Blaster emulation under NTVDM.EXE
 *            Workaround: use the SBLASTER environment variable given by NTVDM.EXE itself
 *        - Sun/Oracle VirtualBox SB16 emulation (short DSP blocks fail to trigger IRQ activity)
 *            Workaround: read the SB16 compatible mixer byte to obtain configuration
 *        - Microsoft Virtual PC SB16 emulation (short DSP blocks fail to trigger IRQ activity)
 *            Workaround: read the SB16 compatible mixer byte to obtain configuration */
void sndsb_manual_probe_irq(struct sndsb_ctx *cx) {
#if TARGET_MSDOS == 16 && defined(__COMPACT__)
	/* too much code */
#else
	unsigned int round = 0;
	unsigned char ml,mh,maybe;
	unsigned int patience = 0;
	unsigned short eliminated = 0U,irr,possible;
	unsigned char tries[] = {2,3,5,7,10},tri;
	DEBUG(fprintf(stdout,"Sound blaster IRQ unknown, I'm going to have to probe for it\n"));

	_cli();
	ml = p8259_read_mask(0);	/* IRQ0-7 */
	mh = p8259_read_mask(8);	/* IRQ8-15 */
	p8259_write_mask(0,0xFF);	/* mask off all interrupts */
	p8259_write_mask(8,0xFF);

	/* wait a bit. during the wait, mark off any interrupts
	 * that happen while we're waiting because they're obviously
	 * not coming from the Sound Blaster */
	patience = 250;
	do {
		t8254_wait(t8254_us2ticks(1000));
		irr  = (unsigned short)p8259_read_IRR(0);
		irr |= (unsigned short)p8259_read_IRR(8) << 8U;
		for (tri=0;tri < sizeof(tries);tri++) {
			if (irr & (1U << tries[tri])) {
				eliminated |= 1U << tries[tri];
			}
		}
	} while (--patience != 0);
	DEBUG(fprintf(stdout,"Pre-test IRQ elimination: 0x%04X\n",eliminated));

	/* restore interrupt mask */
	p8259_write_mask(0,ml);
	p8259_write_mask(8,mh);
	_sti();

	round = 0;
	do {
		if (++round >= 8)
			break;

		/* go through the remaining ones, one at a time */
		possible = 0;
		for (tri=0;tri < sizeof(tries);tri++) {
			if (eliminated & (1U << tries[tri]))
				continue;
			if (!sndsb_reset_dsp(cx)) {
				DEBUG(fprintf(stdout,"WARNING: DSP reset failed, aborting IRQ probe\n"));
				break;
			}

			DEBUG(fprintf(stdout,"  Now testing IRQ %u\n",tries[tri]));

			/* clear SoundBlaster's previous interrupt */
			inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS);

			_cli();
			p8259_write_mask(0,0xFF);	/* mask off all interrupts */
			p8259_write_mask(8,0xFF);

			/* did this IRQ already trigger? then the SB didn't do it */
			irr = (unsigned short)p8259_read_IRR(tries[tri]);
			if (irr & (1 << (tries[tri] & 7))) {
				eliminated |= 1U << tries[tri];
				DEBUG(fprintf(stdout,"Caught IRQ prior to DSP command, updating IRQ elimination: 0x%04x\n",eliminated));
				continue;
			}

			/* make the SoundBlaster trigger an interrupt */
			if (!sndsb_write_dsp(cx,0xF2)) {
				if (!sndsb_write_dsp(cx,0xF2)) {
					DEBUG(fprintf(stdout,"WARNING: DSP write failed, aborting IRQ probe\n"));
					break;
				}
			}

			/* wait for IRQ to show response */
			maybe = 0;
			patience = 10;
			do {
				irr = (unsigned short)p8259_read_IRR(tries[tri]);
				if (irr & (1 << (tries[tri] & 7))) {
					maybe = 1;
					break;
				}
				t8254_wait(t8254_us2ticks(1000));
			} while (--patience != 0);

			DEBUG(fprintf(stdout,"    maybe=%u\n",maybe));
			if (maybe == 0)
				continue;

			/* restore interrupt mask */
			p8259_write_mask(0,ml);
			p8259_write_mask(8,mh);
			_sti();

			if (!sndsb_reset_dsp(cx)) {
				DEBUG(fprintf(stdout,"WARNING: DSP reset failed, aborting IRQ probe\n"));
				break;
			}

			/* clear SoundBlaster's previous interrupt */
			inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS);

			_cli();
			p8259_write_mask(0,0xFF);	/* mask off all interrupts */
			p8259_write_mask(8,0xFF);

			/* did this IRQ already trigger? then the SB didn't do it */
			irr = (unsigned short)p8259_read_IRR(tries[tri]);
			if (irr & (1 << (tries[tri] & 7))) {
				eliminated |= 1U << tries[tri];
				DEBUG(fprintf(stdout,"Caught IRQ prior to DSP command, updating IRQ elimination: 0x%04x\n",eliminated));
				continue;
			}

			/* make the SoundBlaster trigger an interrupt */
			if (!sndsb_write_dsp(cx,0xF2)) {
				if (!sndsb_write_dsp(cx,0xF2)) {
					DEBUG(fprintf(stdout,"WARNING: DSP write failed, aborting IRQ probe\n"));
					break;
				}
			}

			/* wait for IRQ to show response */
			maybe = 0;
			patience = 10;
			do {
				irr = (unsigned short)p8259_read_IRR(tries[tri]);
				if (irr & (1 << (tries[tri] & 7))) {
					maybe = 1;
					break;
				}
				t8254_wait(t8254_us2ticks(1000));
			} while (--patience != 0);

			DEBUG(fprintf(stdout,"    maybe2=%u\n",maybe));
			if (maybe == 0)
				continue;

			possible |= 1U << tries[tri];
		}
		/* loop while we see possibilities, but more than one IRQ appears to be it */
		DEBUG(fprintf(stdout,"Round %u result: possible=0x%04x\n",possible));
	} while (possible != 0 && (possible&(possible-1)) != 0);

	if (possible != 0 && (possible&(possible-1)) == 0) {
		for (tri=0;tri < sizeof(tries);tri++) {
			if (possible & (1U << tries[tri])) {
				cx->irq = tries[tri];
				break;
			}
		}
	}

	/* restore interrupt mask */
	p8259_write_mask(0,ml);
	p8259_write_mask(8,mh);
	_sti();
#endif
}

void sndsb_manual_probe_high_dma(struct sndsb_ctx *cx) {
#if TARGET_MSDOS == 16 && defined(__COMPACT__)
	/* too much code */
#else
	/* NTS: Original code test-played 8192 bytes at 8KHz.
	 *      On every test machine, this meant a considerably long delay when probing.
	 *      To help speed it up, we now play a much shorter sample at 22KHz.
	 *      Unfortunately a sample playback block that short doesn't trigger an IRQ
	 *      under certain emulators like VirtualBox or Virtual PC. If we detect that we're
	 *      running under such emulators we then use a longer block size. */
	unsigned int testlen = 22050/20; /* 1/20th of a second */
	unsigned char tries[] = {5,6,7},tri;
	unsigned int srate = 22050;
	unsigned char dma_count_began = 0;
	unsigned char eliminated = 0;
	uint16_t prev[sizeof(tries)];
	unsigned int patience = 0,rem;
	struct dma_8237_allocation *dma;
	DEBUG(fprintf(stdout,"Sound blaster high DMA unknown, I'm going to have to probe for it\n"));

	if (windows_mode != WINDOWS_NT) {
		/* Sun/Oracle VirtualBox: Sound transfers that are too short are dropped without any
		 * IRQ signal from the emulated SB16 card. Apparently this also has to do with a bug
		 * in their DMA controller emulation where 'terminal count' is the original programmed
		 * value rather than the 0xFFFF value most DMA controllers return. In other words,
		 * we're compensating for VirtualBox's mediocre DMA emulation. */
		if (detect_virtualbox_emu()) {
			DEBUG(fprintf(stdout,"Setting test duration to longer period to work with VirtualBox\n"));
			testlen = 22050/5;
		}
	}

	/* sit back for a bit and watch the DMA channels. if any of them
	 * are cycling, then they are active. NTS: Because the SB16 is
	 * the only one using high DMA and it has a function to tell us
	 * directly, we only probe the lower 8-bit channels */
	_cli();
	for (tri=0;tri < sizeof(tries);tri++) prev[tri] = d8237_read_count_lo16(tries[tri]);
	patience = 500;
	do {
		for (tri=0;tri < sizeof(tries);tri++) {
			if (eliminated & (1U << tries[tri]))
				continue;
			if (prev[tri] != d8237_read_count_lo16(tries[tri]))
				eliminated |= 1U << tries[tri];
		}
	} while (--patience != 0);
	DEBUG(fprintf(stdout,"Pre-test DMA elimination 0x%02x\n",eliminated));

	dma = dma_8237_alloc_buffer(testlen);
	if (dma != NULL) {
#if TARGET_MSDOS == 32
		memset(dma->lin,0,testlen);
#else
		_fmemset(dma->lin,0,testlen);
#endif

		for (tri=0;tri < sizeof(tries);tri++) {
			if (eliminated & (1U << tries[tri]))
				continue;
			if (!sndsb_reset_dsp(cx))
				break;

			/* clear SoundBlaster's previous interrupt */
			/* note that some emulations of the card will fail to play the block
			 * unless we clear the interrupt status. */
			inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS);
			inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS16);

			DEBUG(fprintf(stdout,"     Testing DMA channel %u\n",tries[tri]));

			/* set up the DMA channel */
			outp(d8237_ioport(tries[tri],D8237_REG_W_SINGLE_MASK),
				D8237_MASK_CHANNEL(tries[tri]) | D8237_MASK_SET); /* mask */
			outp(d8237_ioport(tries[tri],D8237_REG_W_WRITE_MODE),
				D8237_MODER_CHANNEL(tries[tri]) |
				D8237_MODER_TRANSFER(D8237_MODER_XFER_READ) |
				D8237_MODER_MODESEL(D8237_MODER_MODESEL_SINGLE));
			d8237_write_base(tries[tri],dma->phys); /* RAM location with not much around */
			d8237_write_count(tries[tri],testlen);
			outp(d8237_ioport(tries[tri],D8237_REG_W_SINGLE_MASK),
				D8237_MASK_CHANNEL(tries[tri])); /* unmask */

			/* Time Constant */
			if (!sndsb_write_dsp_outrate(cx,srate))
				continue;

			/* play a short block */
			if (!sndsb_write_dsp(cx,0xB0|0x02)) continue; /* 16-bit single block FIFO on */
			if (!sndsb_write_dsp(cx,0x10)) continue; /* mono signed */
			if (!sndsb_write_dsp(cx,testlen)) continue;
			if (!sndsb_write_dsp(cx,testlen>>8)) continue;
			DEBUG(fprintf(stdout,"        DSP block started\n",tries[tri]));

			/* wait */
			dma_count_began = 0;
			patience = (unsigned int)(((unsigned long)testlen * 1500UL) / (unsigned long)srate);
			do {
				rem = d8237_read_count(tries[tri]);
				if (rem <= 2 || rem >= 0xFFFF) break; /* if below 2 or at terminal count */

				/* explanation: it turns out some emulation software doesn't quite do the DMA
				 * controllers correctly: on terminal count their counter register reverts to
				 * the value we originally set it to, rather than 0xFFFF. so to detect terminal
				 * count we have to watch it count down, then return to 0xFFFF or to it's
				 * original value.
				 *
				 * This hack is necessary to detect DMA cycling under Sun/Oracle VirtualBox */
				if (dma_count_began) {
					if (rem == testlen) {
						DEBUG(fprintf(stdout,
				"DMA controller snafu: Terminal count appears to be the original\n"
				"counter value, not the 0xFFFF value returned by most controllers.\n"
				"Expect other DOS programs to choke on it too!\n"));
						rem = 0;
						break;
					}
				}
				else {
					if (rem != testlen)
						dma_count_began = 1;
				}

				t8254_wait(t8254_us2ticks(1000));
			} while (--patience != 0);
			if (rem >= 0xFFFF) rem = 0; /* the DMA counter might return 0xFFFF when terminal count reached */
			outp(d8237_ioport(tries[tri],D8237_REG_W_SINGLE_MASK),
				D8237_MASK_CHANNEL(tries[tri]) | D8237_MASK_SET); /* mask */
			sndsb_reset_dsp(cx);

			/* clear SoundBlaster's previous interrupt */
			inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS);
			inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS16);

			if ((unsigned int)(rem+1) < testlen) { /* it moved, this must be the right one */
				DEBUG(fprintf(stdout,"        This one changed, must be the right one\n"));
				cx->dma16 = tries[tri];
				break;
			}
		}

		dma_8237_free_buffer(dma);
	}

	_sti();
#endif
}

void sndsb_manual_probe_dma(struct sndsb_ctx *cx) {
#if TARGET_MSDOS == 16 && defined(__COMPACT__)
	/* too much code */
#else
	/* NTS: Original code test-played 8192 bytes at 8KHz.
	 *      On every test machine, this meant a considerably long delay when probing.
	 *      To help speed it up, we now play a much shorter sample at 22KHz.
	 *      Unfortunately a sample playback block that short doesn't trigger an IRQ
	 *      under certain emulators like VirtualBox or Virtual PC. If we detect that we're
	 *      running under such emulators we then use a longer block size. */
	unsigned char timeconst = (unsigned char)((65536UL - (256000000UL / 22050UL)) >> 8UL);
	unsigned int testlen = 22050/20; /* 1/20th of a second */
	unsigned char tries[] = {0,1,3},tri;
	unsigned int srate = 22050;
	unsigned char dma_count_began = 0;
	unsigned char eliminated = 0;
	uint16_t prev[sizeof(tries)];
	unsigned int patience = 0,rem;
	struct dma_8237_allocation *dma;
	DEBUG(fprintf(stdout,"Sound blaster DMA unknown, I'm going to have to probe for it\n"));

	if (windows_mode != WINDOWS_NT) {
		/* Sun/Oracle VirtualBox: Sound transfers that are too short are dropped without any
		 * IRQ signal from the emulated SB16 card. Apparently this also has to do with a bug
		 * in their DMA controller emulation where 'terminal count' is the original programmed
		 * value rather than the 0xFFFF value most DMA controllers return. In other words,
		 * we're compensating for VirtualBox's mediocre DMA emulation. */
		if (detect_virtualbox_emu()) {
			DEBUG(fprintf(stdout,"Setting test duration to longer period to work with VirtualBox\n"));
			testlen = 22050/5;
		}
	}

	/* sit back for a bit and watch the DMA channels. if any of them
	 * are cycling, then they are active. NTS: Because the SB16 is
	 * the only one using high DMA and it has a function to tell us
	 * directly, we only probe the lower 8-bit channels */
	_cli();
	for (tri=0;tri < sizeof(tries);tri++) prev[tri] = d8237_read_count_lo16(tries[tri]);
	patience = 500;
	do {
		for (tri=0;tri < sizeof(tries);tri++) {
			if (eliminated & (1U << tries[tri]))
				continue;
			if (prev[tri] != d8237_read_count_lo16(tries[tri]))
				eliminated |= 1U << tries[tri];
		}
	} while (--patience != 0);
	DEBUG(fprintf(stdout,"Pre-test DMA elimination 0x%02x\n",eliminated));

	dma = dma_8237_alloc_buffer(testlen);
	if (dma != NULL) {
#if TARGET_MSDOS == 32
		memset(dma->lin,128,testlen);
#else
		_fmemset(dma->lin,128,testlen);
#endif

		/* then, initiate short playback tests to figure out which one */
		/* EMULATOR NOTES:
		 *      - Microsoft Virtual PC:              works
		 *      - DOSBox:                            works
		 *      - Sun/Oracle VirtualBox:             works
		 *
		 * Some emulators like VPC and VirtualBox are not concerned with
		 * accurate emulation. Unfortunately for us this means any attempt
		 * to play really short blocks would fail, because those emulators
		 * would just drop the block and not fire the IRQ. */
		for (tri=0;tri < sizeof(tries);tri++) {
			if (eliminated & (1U << tries[tri]))
				continue;
			if (!sndsb_reset_dsp(cx))
				break;

			/* clear SoundBlaster's previous interrupt */
			/* note that some emulations of the card will fail to play the block
			 * unless we clear the interrupt status. */
			inp(cx->baseio+SNDSB_BIO_DSP_READ_STATUS);

			DEBUG(fprintf(stdout,"     Testing DMA channel %u\n",tries[tri]));

			/* set up the DMA channel */
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(tries[tri]) | D8237_MASK_SET); /* mask */
			outp(D8237_REG_W_WRITE_MODE,
					D8237_MODER_CHANNEL(tries[tri]) |
					D8237_MODER_TRANSFER(D8237_MODER_XFER_READ) |
					D8237_MODER_MODESEL(D8237_MODER_MODESEL_SINGLE));
			d8237_write_base(tries[tri],dma->phys); /* RAM location with not much around */
			d8237_write_count(tries[tri],testlen);
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(tries[tri])); /* unmask */

			/* Time Constant */
			if (!sndsb_write_dsp_timeconst(cx,timeconst))
				continue;

			/* play a short block */
			if (!sndsb_write_dsp(cx,0x14)) continue;
			if (!sndsb_write_dsp(cx,testlen-1)) continue;
			if (!sndsb_write_dsp(cx,(testlen-1)>>8)) continue;
			DEBUG(fprintf(stdout,"        DSP block started\n",tries[tri]));

			/* wait */
			dma_count_began = 0;
			patience = (unsigned int)(((unsigned long)testlen * 1500UL) / (unsigned long)srate);
			do {
				rem = d8237_read_count(tries[tri]);
				if (rem <= 2 || rem >= 0xFFFF) break; /* if below 2 or at terminal count */

				/* explanation: it turns out some emulation software doesn't quite do the DMA
				 * controllers correctly: on terminal count their counter register reverts to
				 * the value we originally set it to, rather than 0xFFFF. so to detect terminal
				 * count we have to watch it count down, then return to 0xFFFF or to it's
				 * original value.
				 *
				 * This hack is necessary to detect DMA cycling under Sun/Oracle VirtualBox */
				if (dma_count_began) {
					if (rem == testlen) {
						DEBUG(fprintf(stdout,
				"DMA controller snafu: Terminal count appears to be the original\n"
				"counter value, not the 0xFFFF value returned by most controllers.\n"
				"Expect other DOS programs to choke on it too!\n"));
						rem = 0;
						break;
					}
				}
				else {
					if (rem != testlen)
						dma_count_began = 1;
				}

				t8254_wait(t8254_us2ticks(1000));
			} while (--patience != 0);
			if (rem >= 0xFFFF) rem = 0; /* the DMA counter might return 0xFFFF when terminal count reached */
			outp(D8237_REG_W_SINGLE_MASK,D8237_MASK_CHANNEL(tries[tri]) | D8237_MASK_SET); /* mask */
			sndsb_reset_dsp(cx);

			if ((unsigned int)(rem+1) < testlen) { /* it moved, this must be the right one */
				DEBUG(fprintf(stdout,"        This one changed, must be the right one\n"));
				cx->dma8 = tries[tri];
				break;
			}
		}

		dma_8237_free_buffer(dma);
	}

	_sti();
#endif
}

/* this is for taking a base address and probing the I/O ports there to see if something like a SB DSP is there. */
/* it is STRONGLY recommended that you don't do this unless you try only 0x220 or 0x240 and you know that nothing
 * else important is there */
int sndsb_try_base(uint16_t iobase) {
	struct sndsb_ctx *cx;

	if ((iobase&0xF) != 0)
		return 0;
	if (iobase < 0x210 || iobase > 0x270)
		return 0;
	if (sndsb_by_base(iobase) != NULL)
		return 0;

	/* some of our detection relies on knowing what OS we're running under */
	cpu_probe();
	probe_dos();
	detect_windows();

	cx = sndsb_alloc_card();
	if (cx == NULL) return 0;

	DEBUG(fprintf(stdout,"sndsb_try_base(0x%03X)\n",iobase));

	cx->baseio = iobase;
	cx->dma8 = cx->dma16 = cx->irq = -1; /* NTS: zero HERE, the init card routine might figure them out */
	if (!sndsb_init_card(cx)) {
		DEBUG(fprintf(stdout,"failed to init card\n"));
		sndsb_free_card(cx);
		return 0;
	}

	/* if we still have to figure out the IRQ, then probe around to figure it out */
	if (!cx->is_gallant_sc6600 && cx->irq == -1 && windows_mode == WINDOWS_NONE && !sndsb_probe_options.disable_manual_irq_probing)
		sndsb_manual_probe_irq(cx);

	/* if we have to, detect the DMA channel. */
	if (!cx->is_gallant_sc6600 && cx->dma8 == -1 && !sndsb_probe_options.disable_manual_dma_probing)
		sndsb_manual_probe_dma(cx);
	/* and the high DMA channel too, if a SB16 or compatible. */
	if (!cx->is_gallant_sc6600 && cx->dma16 == -1 && cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_4xx && !sndsb_probe_options.disable_manual_high_dma_probing)
		sndsb_manual_probe_high_dma(cx);

	/* if we still have to figure out the IRQ, then probe around to figure it out */
	if (!cx->is_gallant_sc6600 && cx->irq == -1 && !sndsb_probe_options.disable_alt_irq_probing)
		sndsb_alt_lite_probe_irq(cx);

	sndsb_determine_ideal_dsp_play_method(cx);
	return 1;
}

int sndsb_interrupt_reason(struct sndsb_ctx *cx) {
	if (cx->dsp_vmaj >= 4) {
	    /* Sound Blaster 16: We can read a mixer byte to determine why the interrupt happened */
	    /* bit 0: 1=8-bit DSP or MIDI */
	    /* bit 1: 1=16-bit DSP */
	    /* bit 2: 1=MPU-401 */
	    return sndsb_read_mixer(cx,0x82) & 7;
	}

	/* DSP 3.xx and earlier: just assume the interrupt happened because of the DSP */
	return 1;
}

int sndsb_reset_mixer(struct sndsb_ctx *cx) {
	if (cx->baseio == 0)
		return 0;

	sndsb_write_mixer(cx,0x00,0x00);	/* "write any 8-bit value to reset the chip" */
	return 1;
}

int sndsb_dsp_out_method_supported(struct sndsb_ctx *cx,unsigned long wav_sample_rate,unsigned char wav_stereo,unsigned char wav_16bit) {
	if (wav_sample_rate < 4000UL) return 0;
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_MAX) return 0;
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_4xx) {
		if (cx->is_gallant_sc6600) {
			if (cx->dsp_vmaj < 3) return 0;
		}
		else {
			if (cx->dsp_vmaj < 4) return 0;
		}
	}
	if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_DIRECT && cx->windows_emulation)
		return 0;
	if (cx->dsp_vmaj >= 4 &&
		(cx->dsp_play_method == SNDSB_DSPOUTMETHOD_201 || cx->dsp_play_method == SNDSB_DSPOUTMETHOD_3xx))
		return 0;
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_201 &&
		(cx->dsp_vmaj < 2 || (cx->dsp_vmaj == 2 && cx->dsp_vmin == 0))) return 0;
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_200 && cx->dsp_vmaj < 2) return 0;
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_1xx && cx->dsp_vmaj < 1) return 0;
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_1xx && (cx->irq < 0 || cx->dma8 < 0)) return 0;
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_4xx && wav_16bit && cx->dma16 < 0) return 0;

	/* alright, check params */
	if (cx->dsp_play_method < SNDSB_DSPOUTMETHOD_3xx && wav_stereo) return 0;
	if (cx->dsp_play_method < SNDSB_DSPOUTMETHOD_4xx && wav_16bit) return 0;
	if (cx->dsp_play_method < SNDSB_DSPOUTMETHOD_4xx && cx->dsp_play_flipped_sign) return 0;

	/* the DSP time interval byte can only go down to 4000Hz */
	if (cx->dsp_play_method < SNDSB_DSPOUTMETHOD_4xx && wav_sample_rate < 4000) return 0;

	if (cx->dsp_adpcm > 0) {
		/* Neither VDMSOUND.EXE or NTVDM's SB emulation handle ADPCM well */
		if (cx->vdmsound || cx->windows_xp_ntvdm) return 0;
		/* Windows 98/ME SBEMUL.SYS handles emulation well, but doesn't do ADPCM either */
		if (cx->windows_9x_me_sbemul_sys) return 0;
		if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_DIRECT) return 0;
		if (wav_16bit || wav_stereo || cx->dsp_play_flipped_sign) return 0;
		if (wav_sample_rate > 12000UL) return 0;
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_4xx) {
		/* based on Sound Blaster 16 PnP cards that max out at 48000Hz apparently */
		if (wav_sample_rate > 48000UL) return 0;
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_201) {/*FIXME: Review Creative SDK documentation. Stereo playback didn't appear until the Pro, right?*/
		if (wav_sample_rate > 44100UL && !wav_stereo) return 0;
		if (wav_sample_rate > 22050UL && wav_stereo) return 0;
	}
	else if (cx->dsp_play_method <= SNDSB_DSPOUTMETHOD_200) {
		if (wav_sample_rate > 23000UL) return 0;
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_DIRECT) {
		if (wav_sample_rate > 23000UL) return 0;
		if (wav_16bit || wav_stereo) return 0;
	}

	return 1;
}

int sndsb_write_dsp_blocksize(struct sndsb_ctx *cx,uint16_t tc) {
	if (!sndsb_write_dsp(cx,0x48))
		return 0;
	if (!sndsb_write_dsp(cx,tc-1))
		return 0;
	if (!sndsb_write_dsp(cx,(tc-1)>>8))
		return 0;
	return 1;
}

int sndsb_write_dsp_outrate(struct sndsb_ctx *cx,unsigned long rate) {
	if (!sndsb_write_dsp(cx,0x41))
		return 0;
	if (!sndsb_write_dsp(cx,rate>>8)) /* Ugh, Creative, be consistent! */
		return 0;
	if (!sndsb_write_dsp(cx,rate))
		return 0;
	return 1;
}

uint32_t sndsb_read_dma_buffer_position(struct sndsb_ctx *cx) {
	uint32_t r;

	/* the program is asking for DMA position. If we're doing the Windows springwait hack,
	 * then NOW is the time to initialize DSP transfer! */
	if (cx->windows_emulation && cx->windows_springwait == 1 && cx->windows_xp_ntvdm) {
		sndsb_prepare_dsp_playback(cx,cx->buffer_rate,cx->buffer_stereo,cx->buffer_16bit);
		sndsb_setup_dma(cx);
		sndsb_begin_dsp_playback(cx);
		cx->windows_springwait = 2;
	}

	if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_DIRECT || cx->goldplay_mode) {
		r = cx->direct_dsp_io; /* caller must fake DMA using direct ADC/DAC and therefore must update this variable */
		/* Goldplay hack mode also requires caller to maintain this pointer */
		if (r >= cx->buffer_size) r = cx->buffer_size - 1;
	}
	else if (cx->buffer_16bit) {
		if (cx->dma16 < 0) return 0;
		r = d8237_read_count(cx->dma16);
		if (r >= 0xFFFEUL) r = 0; /* FIXME: the 8237 library should have a "is terminal count" function */
		if (r >= cx->buffer_dma_started_length) r = cx->buffer_dma_started_length - 1;
		r = cx->buffer_dma_started_length - (r+1);
		r += cx->buffer_dma_started;
	}
	else {
		if (cx->dma8 < 0) return 0;
		r = d8237_read_count(cx->dma8);
		if (r >= 0xFFFFUL) r = 0;
		if (r >= cx->buffer_dma_started_length) r = cx->buffer_dma_started_length - 1;
		r = cx->buffer_dma_started_length - (r+1);
		r += cx->buffer_dma_started;
	}

	return r;
}

int sndsb_shutdown_dma(struct sndsb_ctx *cx) {
	unsigned char ch = cx->buffer_16bit ? cx->dma16 : cx->dma8;
	if ((signed char)ch == -1) return 0;
	/* set up the DMA channel */
	outp(d8237_ioport(ch,D8237_REG_W_SINGLE_MASK),D8237_MASK_CHANNEL(ch) | D8237_MASK_SET); /* mask */
	return 1;
}

int sndsb_setup_dma(struct sndsb_ctx *cx) {
	int ainit = 0;
#if TARGET_MSDOS == 32
	uint64_t phys;
#endif
	unsigned char ch = cx->buffer_16bit ? cx->dma16 : cx->dma8;

	/* if we're doing the Windows "spring" buffer hack, then don't do anything.
	 * later when the calling program queries the DMA position, we'll setup DSP playback and call this function again */
	if (cx->windows_emulation && cx->windows_springwait == 0 && cx->windows_xp_ntvdm)
		return 1;

	cx->direct_dsp_io = 0;
	if ((signed char)ch == -1) return 0;
	/* set up the DMA channel */
	outp(d8237_ioport(ch,D8237_REG_W_SINGLE_MASK),D8237_MASK_CHANNEL(ch) | D8237_MASK_SET); /* mask */

	/* is auto-init involved? */
	if (cx->goldplay_mode)
		ainit = 1;
	else if (cx->dsp_adpcm)
		ainit = (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_200 && cx->enable_adpcm_autoinit) || cx->dsp_1xx_autoinit;
	else if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_200)
		ainit = 1;
	else
		ainit = cx->dsp_1xx_autoinit;

	outp(d8237_ioport(ch,D8237_REG_W_WRITE_MODE),
		(ainit ? D8237_MODER_AUTOINIT : 0) |
		D8237_MODER_CHANNEL(ch) |
		D8237_MODER_TRANSFER(cx->dsp_record ? D8237_MODER_XFER_WRITE : D8237_MODER_XFER_READ) |
		D8237_MODER_MODESEL(D8237_MODER_MODESEL_SINGLE));

	if (cx->goldplay_mode) {
		/* Goldplay mode: The size of ONE sample is given to the DMA controller.
		 * This tricks the DMA controller into re-transmitting that sample continuously
		 * to the sound card. Then the demo uses the timer interrupt to modify that byte
		 * and make audio. This was apparently popular with Goldplay in the 1991-1993
		 * demoscene time frame, and evidently worked fine, but on today's PCs with CPU
		 * caches and buffers this crap would obviously never fly.
		 *
		 * Note we allow the program to do this with 16-bit output, even though the
		 * original Goldplay library was limited to 8 and nobody ever did this kind of
		 * hackery by the time 16-bit SB output was the norm. But my test code shows
		 * that you can pull that stunt with stereo and 16-bit audio modes too! */
		d8237_write_count(ch,(cx->buffer_stereo ? 2 : 1)*(cx->buffer_16bit ? 2 : 1));
		/* point it to our "goldplay_dma" */
#if TARGET_MSDOS == 32
		if (dos_ltp_info.dma_dos_xlate && (size_t)cx->goldplay_dma < 0x100000UL)
			phys = (size_t)cx->goldplay_dma; /* EMM386.EXE intercepts DMA controller I/O and translates for us, it's OK */
		else
			phys = dos_linear_to_phys((size_t)cx->goldplay_dma);

		if (phys == DOS_LTP_FAILED) return 0;
		d8237_write_base(ch,phys);
#else
		{
			unsigned char far *p = (unsigned char far*)(cx->goldplay_dma);
			d8237_write_base(ch,((uint32_t)FP_SEG(p) << 4UL) + (uint32_t)FP_OFF(p));
		}
#endif
		if ((cx->buffer_16bit?1:0)^(cx->dsp_play_flipped_sign?1:0))
			memset(cx->goldplay_dma,0,4);
		else
			memset(cx->goldplay_dma,128,4);
	}
	else {
		d8237_write_count(ch,cx->buffer_dma_started_length);
		d8237_write_base(ch,cx->buffer_phys+cx->buffer_dma_started); /* RAM location with not much around */
	}

	outp(d8237_ioport(ch,D8237_REG_W_SINGLE_MASK),D8237_MASK_CHANNEL(ch)); /* unmask */
	return 1;
}

unsigned long sndsb_real_sample_rate(struct sndsb_ctx *cx) {
	const unsigned long total_rate = (unsigned long)cx->buffer_rate * (cx->buffer_stereo ? 2UL : 1UL);
	const unsigned char timeconst = (unsigned char)((65536UL - (256000000UL / min(total_rate,54000UL))) >> 8UL);
	unsigned long real_rate;

	if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_4xx) return cx->buffer_rate;
	if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_DIRECT) return cx->buffer_rate;

	/* 256 - (1000000 / rate) = const
	 * -(1000000 / rate) = const - 256
	 * 1000000 / rate = -(const - 256)
	 * 1000000 / rate = -const + 256
	 * 1000000 = (-const + 256) * rate
	 * 1000000 / (-const + 256) = rate
	 * 1000000 / (256 - const) = rate */
	real_rate = 1000000UL / (unsigned long)(256 - timeconst);
	if (cx->buffer_stereo) real_rate /= 2UL;
	return real_rate;
}

unsigned char sndsb_rate_to_time_constant(unsigned long rate) {
	return (unsigned char)((65536UL - (((256000000UL + (rate>>1UL)) / rate) + 128UL)) >> 8UL);
}

int sndsb_prepare_dsp_playback(struct sndsb_ctx *cx,unsigned long rate,unsigned char stereo,unsigned char bit16) {
	unsigned long lm;

	if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_DIRECT && cx->windows_emulation)
		return 0;

	/* set up the params. if we already did (windows spring hack) then don't do it again, but proceed directly
	 * to programming the hardware */
	if (cx->windows_springwait == 0) {
		cx->buffer_stereo = stereo;
		cx->buffer_16bit = bit16;
		cx->buffer_rate = rate;
		cx->buffer_hispeed = 0;
		cx->buffer_dma_started = 0;
		cx->buffer_dma_started_length = cx->buffer_size;
		cx->buffer_last_io = 0;
		cx->dsp_stopping = 0;

		lm = cx->buffer_size;
		if (cx->dsp_adpcm == 0) {
			if (bit16) lm >>= 1UL;
			if (stereo) lm >>= 1UL;
		}

		/* if IRQ interval is not assigned, give it the buffer length.
		   we must also ensure the requested interval is less than the
		   buffer length. */
		if (cx->buffer_irq_interval == 0 ||
			cx->buffer_irq_interval > lm)
			cx->buffer_irq_interval = lm;

		/* Windows XP SB emulation: Microsoft's shameful NTVDM.EXE Sound Blaster emulation
		 * attempts to mimic the auto-init modes of DSP v2.0/v2.1 but has a very stupid bug:
		 * if the interval (DSP block size) you specify is not precisely 1/1, 1/2, 1/4, etc.
		 * of the total buffer size (DMA transfer length), their implementation will miss the
		 * end of the DMA transfer and run off into the weeds.
		 *
		 * Another bug: if the block size is too large (4KB or larger?!?) their implementation
		 * will randomly drop portions of the audio and the audio will seem to play extra fast.
		 *
		 * So we have to restrict the irq interval according to these stupid bugs in order to
		 * produce anything close to glitch free audio when under Windows XP's DOS box.
		 *
		 * Shame on you, Microsoft! */
		if (cx->windows_emulation && cx->windows_xp_ntvdm) {
			if (cx->buffer_irq_interval <= (lm / 16UL) || (cx->buffer_size/8) > 4096)
				cx->buffer_irq_interval = (lm / 16UL);
			else if (cx->buffer_irq_interval <= (lm / 8UL) || (cx->buffer_size/4) > 4096)
				cx->buffer_irq_interval = (lm / 8UL);
			else if (cx->buffer_irq_interval <= (lm / 4UL) || (cx->buffer_size/2) > 4096)
				cx->buffer_irq_interval = (lm / 4UL);
			/* Microsoft's shitty implementation also doesn't mesh well with our circular buffer
			 * implementation when the interval is equal to the buffer size. Ther implementation
			 * makes no effort to simulate a DMA transfer going along at the sample rate, it just
			 * "jumps" forward on IRQ. Just as bad as Gravis's SBOS emulation and their shitty
			 * DMA timing. */
			else
				cx->buffer_irq_interval = (lm / 2UL);
		}

		/* don't let the API play 16-bit audio if less than DSP 4.xx because 16-bit audio played
		 * as 8-bit sounds like very loud garbage, be kind to the user */
		if (cx->dsp_play_method < SNDSB_DSPOUTMETHOD_4xx && (bit16 || cx->dsp_play_flipped_sign))
			return 0;
		if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_1xx && (cx->dma8 < 0 || cx->irq < 0))
			return 0;
		if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_4xx && bit16 && cx->dma16 < 0)
			return 0;
		if (cx->dsp_play_method < SNDSB_DSPOUTMETHOD_4xx && rate < 4000)
			return 0;
	}

	/* if we're doing the Windows "spring" buffer hack, then don't do anything.
	 * later when the calling program queries the DMA position, we'll setup DSP playback and call this function again */
	if (cx->windows_emulation && cx->windows_springwait == 0 && cx->windows_xp_ntvdm)
		return 1;

	/* clear any pending DSP events (DSP 4.xx) */
	if (cx->dsp_vmaj >= 4)
		sndsb_interrupt_ack(cx,3);

	/* NTS: I have an old CT1350 that requires the "speaker on" command
	 * even for direct command (0x10) audio to work (or else, you get a
	 * quiet staticky sound that resembles your audio). So while this
	 * command is pointless for Sound Blaster 16 and later, it is vital
	 * for older Sound Blasters.
	 *
	 * CT1350 Detail: DSP v2.2, no mixer chip, does not support stereo,
	 *                maxes out at 44.1KHz, and on the Pentium MMX 200MHz
	 *                system I test it on the DSP has problems playing at
	 *                22050Hz if the floppy drive is running (the audio
	 *                audibly warbles). The card is 8-bit ISA. I also
	 *                noticed modern computer mics don't work with it.
	 *                It was designed for unpowered mics, which were
	 *                common at the time and often used with tape recorders.  */
	sndsb_write_dsp(cx,cx->dsp_record ? 0xD3 : 0xD1); /* turn off speaker if recording, else, turn on */

	if (cx->dsp_adpcm > 0 && cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_1xx) {
		sndsb_write_dsp_timeconst(cx,sndsb_rate_to_time_constant(min(rate,22222UL)));
		if (stereo || bit16 || cx->dsp_record)
			return 0; /* ADPCM modes do not support stereo or 16 bit nor recording */
		if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_200)
			sndsb_write_dsp_blocksize(cx,cx->buffer_irq_interval);
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_1xx) {
		/* NTS: Apparently, issuing Pause & Resume commands at this stage hard-crashes DOSBox 0.74? */
		sndsb_write_dsp_timeconst(cx,sndsb_rate_to_time_constant(min(rate,22222UL)));
		/* Gravis Ultrasound SBOS/MEGA-EM don't handle auto-init 1.xx very well.
		   the only way to cooperate with their shitty emulation is to strictly
		   limit DMA count to the IRQ interval and to NOT set the auto-init flag */
		if (!cx->dsp_1xx_autoinit)
			cx->buffer_dma_started_length = cx->buffer_irq_interval * (stereo?2:1);
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_200) {
		/* NTS: Apparently, issuing Pause & Resume commands at this stage hard-crashes DOSBox 0.74? */
		sndsb_write_dsp_blocksize(cx,cx->buffer_irq_interval * (stereo?2:1));
		sndsb_write_dsp_timeconst(cx,sndsb_rate_to_time_constant(min(rate,22222UL)));
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_201) {/*FIXME: Review Creative SDK documentation. Stereo playback didn't appear until the Pro, right?*/
		/* NTS: Apparently, issuing Pause & Resume commands at this stage hard-crashes DOSBox 0.74? */
		sndsb_write_dsp_blocksize(cx,cx->buffer_irq_interval * (stereo?2:1));
		sndsb_write_dsp_timeconst(cx,sndsb_rate_to_time_constant(min(rate,44444UL)));
		/* NTS: I have a CT1350B card that has audible problems with the ISA bus when driven up to
		 *      22050Hz in non-hispeed modes (if I have something else run, like reading the floppy
		 *      drive, the audio "warbles", changing speed periodically). So while Creative suggests
		 *      enabling hispeed mode for rates 23KHz and above, I think it would be wiser instead
		 *      to do hispeed mode for 16KHz or higher instead. [1]
		 *         [DSP v2.2 with no copyright string]
		 *         [Tested on Pentium MMX 200MHz system with ISA and PCI slots]
		 *         [Applying fix [1] indeed resolved the audible warbling]
		 *         [Is this fix needed for any other Sound Blaster products of that era?] */
		if (cx->dsp_vmaj == 2 && cx->dsp_vmin == 2 && !strcmp(cx->dsp_copyright,"")) /* [1] */
			cx->buffer_hispeed = (rate >= (cx->dsp_record ? 8000 : 16000));
		else
			cx->buffer_hispeed = (rate >= (cx->dsp_record ? 11111 : 23000));
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_3xx) {
		unsigned long total_rate = min(rate,44444UL) * (cx->buffer_stereo ? 2UL : 1UL);
		/* NTS: Apparently, issuing Pause & Resume commands at this stage hard-crashes DOSBox 0.74? */
		if (cx->dsp_record) sndsb_write_dsp(cx,cx->buffer_stereo ? 0xA8 : 0xA0);
		sndsb_write_mixer(cx,0x0E,0x20 | (cx->buffer_stereo ? 0x02 : 0x00));
		sndsb_write_dsp_blocksize(cx,cx->buffer_irq_interval * (stereo?2:1));
		sndsb_write_dsp_timeconst(cx,sndsb_rate_to_time_constant(total_rate));
		cx->buffer_hispeed = (total_rate >= 23000);
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_4xx) {
		sndsb_write_dsp_outrate(cx,min(rate,60000UL));
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_DIRECT) {
	}

	return 1;
}

int sndsb_begin_dsp_playback(struct sndsb_ctx *cx) {
	/* defer beginning playback until the program first asks for the DMA position */
	if (cx->windows_emulation && cx->windows_springwait == 0 && cx->windows_xp_ntvdm) {
		cx->windows_springwait = 1;
		return 1;
	}

	if (cx->dsp_adpcm > 0) {
		if (cx->dsp_record)
			return 0;

		if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_200 && cx->enable_adpcm_autoinit) {
			if (cx->dsp_adpcm == ADPCM_4BIT)
				sndsb_write_dsp(cx,0x7D); /* with ref. byte */
			else if (cx->dsp_adpcm == ADPCM_2_6BIT)
				sndsb_write_dsp(cx,0x7F); /* with ref. byte */
			else if (cx->dsp_adpcm == ADPCM_2BIT)
				sndsb_write_dsp(cx,0x1F); /* with ref. byte */
		}
		else if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_1xx) {
			unsigned short lv;

			lv = cx->buffer_irq_interval - 1;
			if (cx->dsp_adpcm == ADPCM_4BIT)
				sndsb_write_dsp(cx,0x75); /* with ref. byte */
			else if (cx->dsp_adpcm == ADPCM_2_6BIT)
				sndsb_write_dsp(cx,0x77); /* with ref. byte */
			else if (cx->dsp_adpcm == ADPCM_2BIT)
				sndsb_write_dsp(cx,0x17); /* with ref. byte */
			sndsb_write_dsp(cx,lv);
			sndsb_write_dsp(cx,lv >> 8);
		}
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_1xx) {
		unsigned short lv = (cx->buffer_irq_interval * (cx->buffer_stereo?2:1)) - 1;
		sndsb_write_dsp(cx,cx->dsp_record ? 0x24 : 0x14);
		sndsb_write_dsp(cx,lv);
		sndsb_write_dsp(cx,lv >> 8);
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_200) {
		sndsb_write_dsp(cx,cx->dsp_record ? 0x2C : 0x1C);
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_201) {
		if (cx->buffer_hispeed)
			sndsb_write_dsp(cx,cx->dsp_record ? 0x98 : 0x90);
		else
			sndsb_write_dsp(cx,cx->dsp_record ? 0x2C : 0x1C);
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_3xx) {
		if (cx->buffer_hispeed)
			sndsb_write_dsp(cx,cx->dsp_record ? 0x98 : 0x90);
		else
			sndsb_write_dsp(cx,cx->dsp_record ? 0x2C : 0x1C);
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_4xx) {
		unsigned int samps = cx->buffer_irq_interval * (cx->buffer_stereo?2:1);
		sndsb_write_dsp(cx,(cx->buffer_16bit ? 0xB0 : 0xC0) | 0x04 | 0x02 |
			(cx->dsp_record ? 0x08 : 0x00));	/* bCommand auto-init FIFO on */
		sndsb_write_dsp(cx,(cx->dsp_play_flipped_sign ? 0x10 : 0x00) ^
			((cx->buffer_stereo ? 0x20 : 0x00) | (cx->buffer_16bit ? 0x10 : 0x00))); /* bMode */
		sndsb_write_dsp(cx,samps);
		sndsb_write_dsp(cx,samps>>8);
	}

	return 1;
}

int sndsb_stop_dsp_playback(struct sndsb_ctx *cx) {
	cx->dsp_stopping = 1;
	cx->windows_springwait = 0;

	/* NTS: As far as I can tell, the best way to stop the sound card is just reset the DSP.
	 *      The "Exit auto-init" commands don't seem to work */
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_1xx)
		sndsb_reset_dsp(cx);
	if (cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_3xx && cx->dsp_record)
		sndsb_write_dsp(cx,0xA0);

	if ((cx->buffer_16bit && cx->dma16 >= 0) || (!cx->buffer_16bit && cx->dma8 >= 0)) {
		uint16_t pr,cr;
		unsigned int nonmove = 0;
		/* wait for the DMA channel to stop moving */
		if (cx->buffer_16bit)	cr = d8237_read_count(cx->dma16);
		else			cr = d8237_read_count(cx->dma8);
		do {
			t8254_wait(t8254_us2ticks(10000)); /* 10ms */
			pr = cr;
			if (cx->buffer_16bit)	cr = d8237_read_count(cx->dma16);
			else			cr = d8237_read_count(cx->dma8);
			if (pr == cr) nonmove++;
			else nonmove = 0;
		} while (nonmove < 3);
	}

	if (cx->dsp_play_method > SNDSB_DSPOUTMETHOD_DIRECT) {
		sndsb_shutdown_dma(cx);
		sndsb_write_mixer(cx,0x0E,0);
	}

	sndsb_write_dsp(cx,0xD3); /* turn off speaker */
	return 1;
}

void sndsb_send_buffer_again(struct sndsb_ctx *cx) {
	if (cx->dsp_stopping) return;
	/* if we're doing the one-block-at-a-time 1.xx method, then start another right now */
	if (cx->dsp_adpcm > 0 && cx->dsp_play_method >= SNDSB_DSPOUTMETHOD_1xx &&
		(cx->dsp_play_method < SNDSB_DSPOUTMETHOD_200 || !cx->enable_adpcm_autoinit)) {
		unsigned short lv = cx->buffer_irq_interval - 1;

		/* if we're doing it the non-autoinit method, then we
		   also need to update the DMA pointer */
		if (!cx->dsp_1xx_autoinit) {
			unsigned char ch = cx->buffer_16bit ? cx->dma16 : cx->dma8;
			{
				unsigned long npos = cx->buffer_dma_started_length + cx->buffer_dma_started;
				unsigned long rem = cx->buffer_size - npos;
				if ((signed long)rem < 0L) rem = 0UL;
				if (rem > (unsigned long)lv) rem = (unsigned long)lv;
				if (rem == 0) {
					npos = 0;
					rem = cx->buffer_irq_interval;
					/* ^ warning: assuming interval <= bufsize */
				}
				lv = (unsigned short)rem;
				cx->buffer_dma_started = npos;
				cx->buffer_dma_started_length = rem;
			}

			outp(d8237_ioport(ch,D8237_REG_W_SINGLE_MASK),D8237_MASK_CHANNEL(ch) | D8237_MASK_SET); /* mask */
			d8237_write_base(ch,cx->buffer_phys+cx->buffer_dma_started); /* RAM location with not much around */
			d8237_write_count(ch,cx->buffer_dma_started_length);
			outp(d8237_ioport(ch,D8237_REG_W_SINGLE_MASK),D8237_MASK_CHANNEL(ch)); /* unmask */
		}

		if (cx->dsp_adpcm == ADPCM_4BIT)
			sndsb_write_dsp(cx,0x74); /* without ref. byte */
		else if (cx->dsp_adpcm == ADPCM_2_6BIT)
			sndsb_write_dsp(cx,0x76); /* without ref. byte */
		else if (cx->dsp_adpcm == ADPCM_2BIT)
			sndsb_write_dsp(cx,0x16); /* without ref. byte */
		sndsb_write_dsp(cx,lv);
		sndsb_write_dsp(cx,lv >> 8);
	}
	else if (cx->dsp_play_method == SNDSB_DSPOUTMETHOD_1xx) {
		unsigned short lv = cx->buffer_irq_interval;

		/* if we're doing it the non-autoinit method, then we
		   also need to update the DMA pointer */
		if (!cx->dsp_1xx_autoinit) {
			unsigned char ch = cx->buffer_16bit ? cx->dma16 : cx->dma8;
			{
				unsigned long npos = cx->buffer_dma_started_length + cx->buffer_dma_started;
				unsigned long rem = cx->buffer_size - npos;
				if ((signed long)rem < 0L) rem = 0UL;
				if (rem > (unsigned long)lv) rem = (unsigned long)lv;
				if (rem == 0) {
					npos = 0;
					rem = cx->buffer_irq_interval;
					/* ^ warning: assuming interval <= bufsize */
				}
				lv = (unsigned short)rem;
				cx->buffer_dma_started = npos;
				cx->buffer_dma_started_length = rem;
			}

			outp(d8237_ioport(ch,D8237_REG_W_SINGLE_MASK),D8237_MASK_CHANNEL(ch) | D8237_MASK_SET); /* mask */
			d8237_write_base(ch,cx->buffer_phys+cx->buffer_dma_started); /* RAM location with not much around */
			d8237_write_count(ch,cx->buffer_dma_started_length);
			outp(d8237_ioport(ch,D8237_REG_W_SINGLE_MASK),D8237_MASK_CHANNEL(ch)); /* unmask */
		}

		if (lv != 0) lv--;
		sndsb_write_dsp(cx,cx->dsp_record ? 0x24 : 0x14);
		sndsb_write_dsp(cx,lv);
		sndsb_write_dsp(cx,lv >> 8);
	}
	/* for anything else including no sound (or usually auto-init DMA)
	 * there is nothing to do */
}

void sndsb_choose_mixer(struct sndsb_ctx *card,signed char override) {
	signed char idx;

	card->sb_mixer_items = 0;
	card->sb_mixer = NULL;
	idx = override >= 0 ? override : card->mixer_chip;

	if (idx == SNDSB_MIXER_CT1335) {
		card->sb_mixer = sndsb_mixer_ct1335;
		card->sb_mixer_items = (signed short)(sizeof(sndsb_mixer_ct1335) / sizeof(struct sndsb_mixer_control));
	}
	else if (idx == SNDSB_MIXER_CT1345) {
		card->sb_mixer = sndsb_mixer_ct1345;
		card->sb_mixer_items = (signed short)(sizeof(sndsb_mixer_ct1345) / sizeof(struct sndsb_mixer_control));
	}
	else if (idx == SNDSB_MIXER_CT1745) {
		card->sb_mixer = sndsb_mixer_ct1745;
		card->sb_mixer_items = (signed short)(sizeof(sndsb_mixer_ct1745) / sizeof(struct sndsb_mixer_control));
	}
}

/* NTS: This is the best documentation I could fine regarding the Sound Blaster ADPCM format.
 *      Tables and method taken from DOSBox 0.74 SB emulation. The information on multimedia.cx's
 *      Wiki is wrong. */
unsigned char sndsb_encode_adpcm_4bit(unsigned char samp) {
	static const signed char scaleMap[64] = {
		0,  1,  2,  3,  4,  5,  6,  7,  0,  -1,  -2,  -3,  -4,  -5,  -6,  -7,
		1,  3,  5,  7,  9, 11, 13, 15, -1,  -3,  -5,  -7,  -9, -11, -13, -15,
		2,  6, 10, 14, 18, 22, 26, 30, -2,  -6, -10, -14, -18, -22, -26, -30,
		4, 12, 20, 28, 36, 44, 52, 60, -4, -12, -20, -28, -36, -44, -52, -60
	};
	static const signed char adjustMap[32] = {
		 0, 0, 0, 0, 0, 1, 1, 1,
		-1, 0, 0, 0, 0, 1, 1, 1,
		-1, 0, 0, 0, 0, 1, 1, 1,
		-1, 0, 0, 0, 0, 0, 0, 0
	};
	signed int sdelta = (signed int)((signed char)(samp - adpcm_pred));
	unsigned char sign = 0;

	sdelta = (sdelta * 2) + (adpcm_step < 3 ? adpcm_error : 0);
	adpcm_error = sdelta & ((1 << (adpcm_step + 1)) - 1);
	sdelta >>= adpcm_step+1;
	if (sdelta < 0) {
		sdelta = -sdelta;
		sign = 8;
	}
	if (sdelta > 7) sdelta = 7;
	adpcm_pred += scaleMap[(adpcm_step*16)+sign+sdelta];
	if (adpcm_pred < 0) adpcm_pred = 0;
	else if (adpcm_pred > 0xFF) adpcm_pred = 0xFF;
	adpcm_step += adjustMap[(adpcm_step*8)+sdelta];
	if ((signed char)adpcm_step < 0) adpcm_step = 0;
	if (adpcm_step > 3) adpcm_step = 3;
	return (unsigned char)sdelta | sign;
}

/* NTS: This is the best documentation I could fine regarding the Sound Blaster ADPCM format.
 *      Tables and method taken from DOSBox 0.74 SB emulation. The information on multimedia.cx's
 *      Wiki is wrong. */
unsigned char sndsb_encode_adpcm_2bit(unsigned char samp) {
	static const signed char scaleMap[24] = {
		0,  1,  0,  -1,  1,  3,  -1,  -3,
		2,  6, -2,  -6,  4, 12,  -4, -12,
		8, 24, -8, -24, 16, 48, -16, -48
/* NTS: This table is correct as tested against real Creative SB
        hardware. DOSBox's version has a typo on the last row
	that will make the 2-bit playback sound WORSE in it. */
	};
	static const signed char adjustMap[12] = {
		 0, 1, -1, 1,
		-1, 1, -1, 1,
		-1, 1, -1, 0
	};
	signed int sdelta = (signed int)((signed char)(samp - adpcm_pred));
	unsigned char sign = 0;

	sdelta = (sdelta * 2) + (adpcm_step == 0 ? adpcm_error : 0);
	adpcm_error = sdelta & ((1 << adpcm_step) - 1);
	sdelta >>= adpcm_step+1;

	if (sdelta < 0) {
		sdelta = -sdelta;
		sign = 2;
	}

	/* "ring" suppression */
	if (adpcm_step == 5 && sdelta == 1 && adpcm_last == 3 && sign == 0)
		sdelta = 0;

	if (sdelta > 1) sdelta = 1;
	adpcm_last = sdelta + sign;
	adpcm_pred += scaleMap[(adpcm_step*4)+sign+sdelta];
	if (adpcm_pred < 0) adpcm_pred = 0;
	else if (adpcm_pred > 0xFF) adpcm_pred = 0xFF;
	adpcm_step += adjustMap[(adpcm_step*2)+sdelta];
	if ((signed char)adpcm_step < 0) adpcm_step = 0;
	if (adpcm_step > 5) adpcm_step = 5;
	return (unsigned char)sdelta | sign;
}

/* NTS: This is the best documentation I could fine regarding the Sound Blaster ADPCM format.
 *      Tables and method taken from DOSBox 0.74 SB emulation. The information on multimedia.cx's
 *      Wiki is wrong. */
unsigned char sndsb_encode_adpcm_2_6bit(unsigned char samp,unsigned char b2) {
	static const signed char scaleMap[40] = {
		0,  1,  2,  3,  0,  -1,  -2,  -3,
		1,  3,  5,  7, -1,  -3,  -5,  -7,
		2,  6, 10, 14, -2,  -6, -10, -14,
		4, 12, 20, 28, -4, -12, -20, -28,
		5, 15, 25, 35, -5, -15, -25, -35
	};
	static const signed char adjustMap[20] = {
		 0, 0, 0, 1,
		-1, 0, 0, 1,
		-1, 0, 0, 1,
		-1, 0, 0, 1,
		-1, 0, 0, 0
	};
	signed int sdelta = (signed int)((signed char)(samp - adpcm_pred));
	unsigned char sign = 0;

	sdelta = (sdelta * 2) + (adpcm_step < 2 ? adpcm_error : 0);
	adpcm_error = sdelta & ((1 << (adpcm_step + (b2 ? 2 : 1))) - 1);
	sdelta >>= adpcm_step+1;

	if (sdelta < 0) {
		sdelta = -sdelta;
		sign = 4;
	}

	if (sdelta > 3) sdelta = 3;
	sdelta += sign;
	if (b2) sdelta &= 0x6;
	adpcm_pred += scaleMap[(adpcm_step*8)+sdelta];
	if (adpcm_pred < 0) adpcm_pred = 0;
	else if (adpcm_pred > 0xFF) adpcm_pred = 0xFF;
	adpcm_step += adjustMap[(adpcm_step*4)+(sdelta&3)];
	if ((signed char)adpcm_step < 0) adpcm_step = 0;
	if (adpcm_step > 5) adpcm_step = 5;
	return (unsigned char)sdelta;
}

void sndsb_encode_adpcm_set_reference(unsigned char c,unsigned char mode) {
	adpcm_pred = c;
	adpcm_step = 0;
	if (mode == ADPCM_4BIT)
		adpcm_lim = 5;
	else if (mode == ADPCM_2_6BIT)
		adpcm_lim = 3;
	else if (mode == ADPCM_2BIT)
		adpcm_lim = 1;
}

/* undocumented and not properly emulated by DOSBox either:
   when Creative said the non-reference ADPCM commands "continue
   using accumulated reference byte" they apparently meant that
   it resets the step value to max. Yes, even in auto-init
   ADPCM mode. Failure to follow this results in audible
   "fluttering" once per IRQ. */
void sndsb_encode_adpcm_reset_wo_ref(unsigned char mode) {
	if (mode == ADPCM_4BIT)
		adpcm_step = 3;
	else if (mode == ADPCM_2_6BIT)
		adpcm_step = 4;
	else
		adpcm_step = 5; /* FIXME: Testing by ear seems to favor this one. Is this correct? */
}

void sndsb_write_mixer_entry(struct sndsb_ctx *sb,struct sndsb_mixer_control *mc,unsigned char nb) {
	unsigned char b;
	if (mc->length == 0) return;
	else if (mc->length == 8) {
		sndsb_write_mixer(sb,mc->index,nb);
	}
	else {
		b = sndsb_read_mixer(sb,mc->index);
		b &= ~(((1 << mc->length) - 1) << mc->offset);
		b |= (nb & ((1 << mc->length) - 1)) << mc->offset;
		sndsb_write_mixer(sb,mc->index,b);
	}
}

unsigned char sndsb_read_mixer_entry(struct sndsb_ctx *sb,struct sndsb_mixer_control *mc) {
	unsigned char b;
	if (mc->length == 0) return 0;
	b = sndsb_read_mixer(sb,mc->index);
	return (b >> mc->offset) & ((1 << mc->length) - 1);
}

int sndsb_assign_dma_buffer(struct sndsb_ctx *cx,struct dma_8237_allocation *dma) {
	cx->buffer_size = dma->length;
	cx->buffer_phys = dma->phys;
	return 1;
}

uint32_t sndsb_recommended_dma_buffer_size(struct sndsb_ctx *ctx,uint32_t limit) {
	uint32_t ret = 60UL * 1024UL;
	if (limit != 0UL && ret > limit) ret = limit;

	/* Known constraint: Windows 3.1 Creative SB16 drivers don't like it when DOS apps
	 *                   use too large a DMA buffer. It causes Windows to complain about
	 *                   "a DOS program violating the integrity of the operating system".
	 *
	 *                   FIXME: Even with small buffers, it "violates the integrity" anyway.
	 *                          So what the fuck is wrong then? */
	if (windows_mode == WINDOWS_ENHANCED && windows_version < 0x35F && /* Windows 3.1 and Creative SB16 drivers v3.57 */
		ctx->windows_creative_sb16_drivers && ctx->windows_creative_sb16_drivers_ver == (0x300 + 57)) {
		if (ret > (4UL * 1024UL)) ret = 4UL * 1024UL;
	}

	return ret;
}
