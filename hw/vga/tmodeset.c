
/* TODO: Interesting bugs found
 *
 *
 *      In DOSBox-X: Setting mode 6 (640x200 CGA graphics) leaves div2=1 which DOSBox-X
 *      then mis-emulates as 640x200 2-color at 35Hz not 70Hz */

#include <stdio.h>
#include <conio.h> /* this is where Open Watcom hides the outp() etc. functions */
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <dos.h>

#include <hw/cpu/cpu.h>
#include <hw/dos/dos.h>
#include <hw/vga/vga.h>
#include <hw/vga/vgatty.h>

#if defined(TARGET_WINDOWS)
# error WRONG
#endif

char tmpline[80];

void bios_cls() {
	/* use INT 10h to scroll-clear the screen */
	__asm {
		push	ax
		push	bx
		push	cx
		push	dx
		push	bp
		push	ds

		mov	ax,0x0600
		mov	bh,0x07
		mov	cx,0x0000	/* upper left (x,y) 0,0 */
		mov	dh,24
		mov	dl,79		/* lower right (x,y) 79,24 */
		int	10h

		pop	ds
		pop	bp
		pop	dx
		pop	cx
		pop	bx
		pop	ax
	}
}

unsigned int common_prompt_number() {
	unsigned int nm;

	tmpline[0] = 0;
	fgets(tmpline,sizeof(tmpline),stdin);
	if (isdigit(tmpline[0]))
		nm = (unsigned int)strtoul(tmpline,NULL,0);
	else
		nm = ~0;

	return nm;
}

void help_main() {
	int c;

	bios_cls();
	vga_moveto(0,0);
	vga_sync_bios_cursor();

	printf("VGA library state:\n");
	printf("\n");
	printf(" VGA=        (1) VGA detected\n");
	printf(" EGA=        (1) EGA detected/compat\n");
	printf(" CGA=        (1) CGA detected/compat\n");
	printf(" MDA=        (1) MDA detected/compat\n");
	printf(" MCGA=       (1) MCGA detected/compat\n");
	printf(" HGC=        Hercules detected & what\n");
	printf(" Tandy/PCjr= Tandy or PCjr detected\n");
	printf(" Amstrad=    Amstrad detected\n");
	printf(" IO=         Base I/O port (3B0/3D0)\n");
	printf(" ALPHA=      (1) Alphanumeric mode\n");
	printf(" RAM         VGA RAM mapping\n");
	printf(" 9W=         (1) VGA lib 9-pixel mode\n");
	printf(" INT10=      Active INT10h video mode\n");
	printf("\n");
	printf("ESC to return, ENTER/SPACE for more...\n");

	c = getch();
	if (c == 27) return;

	bios_cls();
	vga_moveto(0,0);
	vga_sync_bios_cursor();

	printf("VGA CRTC state:\n");
	printf("\n");
	printf(" Clock=      Dot clock selection (0-3)\n");
	printf(" div2=       (1) Divide dot clock by 2\n");
	printf(" pix/clk=    pixels/char clk (8 or 9)\n");
	printf(" Word=       (1) Word mode\n");
	printf(" Dword=      (1) Doubleword mode\n");
	printf(" Hsync       -/+ Hsync polarity\n");
	printf(" Vsync       -/+ Vsync polarity\n");
	printf(" mem4=       Mem clock divide by 4\n");
	printf(" SLR=        Shift/load rate\n");
	printf(" S4=         Shift 4 enable\n");
	printf(" memdv2=     (1) Div memaddr by 2\n");
	printf(" scandv2=    (1) Div scanlineclk by 2\n");
	printf(" awrap=      (1) Address wrap select\n");
	printf(" map14=      (1) Map MA14 = bit 14\n");
	printf(" map13=      (1) Map MA13 = bit 13\n");
	printf(" ref/scanline= Refresh cycles/scanline\n");
	printf(" hrate/vrate= Horz./Vert. refresh rate\n");
	printf("\n");
	printf("ESC to return, ENTER/SPACE for more...\n");

	c = getch();
	if (c == 27) return;

	bios_cls();
	vga_moveto(0,0);
	vga_sync_bios_cursor();

	printf("VGA CRTC state (cont):\n");
	printf("\n");
	printf(" V:tot=      Vertical total\n");
	printf(" disp=       .. active display\n");
	printf(" retr=       .. retrace as inequality\n");
	printf(" blnk=       .. blanking as inequality\n");
	printf(" H:tot=      Horizontal total (chars)\n");
	printf(" disp=       .. active display\n");
	printf(" retr=       .. retrace as inequality\n");
	printf(" blnk=       .. blanking as inequality\n");
	printf(" sdtot=      .. start delay after total\n");
	printf(" sdretr=     .. start delay aft retrace\n");
	printf("\n");
	printf("ESC to return, ENTER/SPACE for more...\n");

	c = getch();
	if (c == 27) return;
}

int main() {
	unsigned char redraw,vga_mode;
	struct vga_mode_params mp;
	int c;

	probe_dos();
	detect_windows();

	if (!probe_vga()) {
		printf("VGA probe failed\n");
		return 1;
	}

	if (!(vga_flags & VGA_IS_VGA)) {
		printf("Modesetting + readback of CRTC registers is only supported on VGA\n");
		return 1;
	}

	redraw = 1;
	while (1) {
		update_state_from_vga();

		if (redraw) {
			unsigned long rate;
			double hrate,vrate;

			redraw = 0;
			vga_mode = int10_getmode();
			vga_read_crtc_mode(&mp);

			rate = vga_clock_rates[mp.clock_select];
			if (mp.clock_div2) rate >>= 1;
			hrate = (double)rate / ((mp.clock9 ? 9 : 8) * mp.horizontal_total);
			vrate = hrate / mp.vertical_total;

			bios_cls();

			/* position the cursor to home */
			vga_moveto(0,0);
			vga_sync_bios_cursor();

			printf("VGA=%u EGA=%u CGA=%u MDA=%u MCGA=%u HGC=%u(%u)\n",
					(vga_flags & VGA_IS_VGA) ? 1 : 0,
					(vga_flags & VGA_IS_EGA) ? 1 : 0,
					(vga_flags & VGA_IS_CGA) ? 1 : 0,
					(vga_flags & VGA_IS_MDA) ? 1 : 0,
					(vga_flags & VGA_IS_MCGA) ? 1 : 0,
					(vga_flags & VGA_IS_HGC) ? 1 : 0,vga_hgc_type);
			printf("Tandy/PCjr=%u Amstrad=%u IO=%03xh ALPHA=%u\n",
					(vga_flags & VGA_IS_TANDY) ? 1 : 0,
					(vga_flags & VGA_IS_AMSTRAD) ? 1 : 0,
					vga_base_3x0,
					vga_alpha_mode);

			printf("RAM 0x%05lx-0x%05lx 9W=%u INT10=%u(0x%02x)\n",
					(unsigned long)vga_ram_base,
					(unsigned long)vga_ram_base+vga_ram_size-1UL,
					vga_9wide,vga_mode,vga_mode);

			printf("Clock=%u div2=%u (%luHZ) pix/clk=%u\n",
					mp.clock_select,
					mp.clock_div2,
					vga_clock_rates[mp.clock_select] >> (mp.clock_div2 ? 1 : 0),
					mp.clock9 ? 9 : 8);

			printf("Word=%u DWord=%u Hsync%c Vsync%c mem4=%u\n",
					mp.word_mode,
					mp.dword_mode,
					mp.hsync_neg?'-':'+',
					mp.vsync_neg?'-':'+',
					mp.inc_mem_addr_only_every_4th);

			printf("SLR=%u S4=%u memdv2=%u scandv2=%u awrap=%u\n",
					mp.shift_load_rate,
					mp.shift4_enable,
					mp.memaddr_div2,
					mp.scanline_div2,
					mp.address_wrap_select);

			printf("map14=%u map13=%u ref/scanline=%u\n",
					mp.map14,
					mp.map13,
					mp.refresh_cycles_per_scanline);

			printf("hrate=%.3fKHz vrate=%.3fHz\n",
					hrate / 1000,
					vrate);

			printf("V:tot=%u disp=%u retr=%u <=x< %u\n",
					mp.vertical_total,
					mp.vertical_display_end,
					mp.vertical_start_retrace,
					mp.vertical_end_retrace);

			printf(" blnk=%u <=x< %u\n",
					mp.vertical_blank_start,
					mp.vertical_blank_end);

			printf("H:tot=%u disp=%u retr=%u <=x< %u\n",
					mp.horizontal_total,
					mp.horizontal_display_end,
					mp.horizontal_start_retrace,
					mp.horizontal_end_retrace);

			printf(" blnk=%u <=x< %u sdtot=%u sdretr=%u\n",
					mp.horizontal_blank_start,
					mp.horizontal_blank_end,
					mp.horizontal_start_delay_after_total,
					mp.horizontal_start_delay_after_retrace);

			printf("\n");

			printf("ESC  Exit to DOS       ? Explain this\n");
			printf("m    Set mode          C Change clock\n");
		}

		c = getch();
		if (c == 27) break;
		else if (c == '?') {
			redraw = 1;
			help_main();
		}
		else if (c == 'm') {
			unsigned int nm;

			printf("\n");
			printf("Mode? "); fflush(stdout);
			nm = common_prompt_number();
			if (nm < 128) {
				int10_setmode(nm);
				redraw = 1;
			}
		}
		else if (c == 'c') {
			unsigned int nm;

			printf("\n");
			for (nm=0;nm < 4;nm++) printf(" [%u] = %-8luHz   [%u] = %luHz\n",
				nm,vga_clock_rates[nm],
				nm+4,vga_clock_rates[nm]>>1);
			printf("Clock? "); fflush(stdout);
			nm = common_prompt_number();
			if (nm != ~0 && nm < 8) {
				mp.clock_select = nm&3;
				mp.clock_div2 = (nm&4)?1:0;
				vga_write_crtc_mode(&mp);
				redraw = 1;
			}
		}
	}

	return 0;
}
