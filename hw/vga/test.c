/* FIXME: This program needs a way to hook into the "winfcon" library. It needs to:
          - Pass all messages through the DispDib hook function.
          - Restore VGA and exit if the user ALT+TAB's out of our app */

#include <stdio.h>
#include <conio.h> /* this is where Open Watcom hides the outp() etc. functions */
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <dos.h>

#include <hw/cpu/cpu.h>
#include <hw/dos/dos.h>
#include <hw/vga/vga.h>

#ifdef TARGET_WINDOWS
# define WINFCON_STOCK_WIN_MAIN
# include <hw/dos/winfcon.h>
# include <windows/apihelp.h>
# include <windows/dispdib/dispdib.h>
# include <windows/win16eb/win16eb.h>
#endif

unsigned char paltmp[392*3];

int main() {
#if defined(TARGET_WINDOWS)
	HWND hwnd;
#endif
	unsigned char vga_want_9wide;
	int c;

	probe_dos();
	detect_windows();

#if defined(TARGET_WINDOWS)
# ifdef WIN_STDOUT_CONSOLE
	hwnd = _win_hwnd();
# else
	hwnd = GetFocus();

	printf("WARNING: The Win32 console version is experimental. It generally works...\n");
	printf("         but can run into problems if focus is taken or if the console window\n");
	printf("         is started minimized or maximized. ENTER to continue, ESC to cancel.\n");
	printf("         If this console is full-screen, hit ALT+ENTER to switch to window.\n");
	do {
		c = getch();
		if (c == 27) return 1;
	} while (c != 13);
# endif

# if TARGET_WINDOWS == 32
	if (!InitWin16EB()) {
		MessageBox(hwnd,"Failed to init win16eb","Win16eb",MB_OK);
		return 1;
	}
# endif

	/* setup DISPDIB */
	if (DisplayDibLoadDLL()) {
		MessageBox(hwnd,dispDibLastError,"Failed to load DISPDIB.DLL",MB_OK);
		return 1;
	}

	if (DisplayDibCreateWindow(hwnd)) {
		MessageBox(hwnd,dispDibLastError,"Failed to create DispDib window",MB_OK);
		return 1;
	}

	if (DisplayDibGenerateTestImage()) {
		MessageBox(hwnd,dispDibLastError,"Failed to generate test card",MB_OK);
		return 1;
	}

	if (DisplayDibDoBegin()) {
		MessageBox(hwnd,dispDibLastError,"Failed to begin display",MB_OK);
		return 1;
	}
#endif

	if (!probe_vga()) { /* NTS: By "VGA" we mean any VGA, EGA, CGA, MDA, or other common graphics hardware on the PC platform
                               that acts in common ways and responds to I/O ports 3B0-3BF or 3D0-3DF as well as 3C0-3CF */
#if defined(TARGET_WINDOWS)
		DisplayDibDoEnd();
		DisplayDibUnloadDLL();
		MessageBox(hwnd,"Probe failed","VGA lib",MB_OK);
#else
		printf("VGA probe failed\n");
#endif
		return 1;
	}

	vga_want_9wide = 0xFF;
	vga_write_state_DEBUG(stdout);
	assert(vga_ram_base != 0UL);
	assert(vga_ram_size != 0UL);
	assert(vga_graphics_ram != NULL);
	assert(vga_alpha_ram != NULL);

#if defined(TARGET_WINDOWS)
	int10_setmode(3); /* 80x25 */
	update_state_from_vga();
#else
	if (!vga_alpha_mode) {
		int10_setmode(3); /* 80x25 */
		update_state_from_vga();
		if (!vga_alpha_mode) {
			printf("Unable to determine that the graphics card is in an alphanumeric mode\n");
			return 1;
		}
	}
#endif

	while (1) {
		vga_clear(); vga_moveto(0,0);
		vga_write_color(0x7);
		vga_write_state_DEBUG(NULL);
		vga_write("\n");
		vga_write("  1: Set video mode   M: Make mono\n");
		vga_write("  T: Restore 80x25    C: Make color\n");
		vga_write("  H: HGC graphics tst !: VGA mmap test\n");
		vga_write("  D: CGA graphics tst P: Tandy graphics tst\n");
		vga_write("  V: EGA/VGA graphics W: EGA/VGA grph II\n");
		vga_write("  X: VGA/MCGA 256-col Y: VGA 256-col modex\n");
		vga_write("  R: VGA raster tst   S: EGA raster tst\n");
		vga_write("  s: EGA/VGA split sc F: VGA/EGA font tst\n");
		vga_write("  x: VGA raster fx    m: VGA modesetting\n");
		vga_write("  A: Amstrad graphics test\n");
		if (vga_9wide)
			vga_write("  9: VGA 8-bit wide \n");
		else
			vga_write("  9: VGA 9-bit wide \n");

		vga_write("ESC: quit\n");
		vga_write_sync();
		vga_sync_bios_cursor();
		c = getch();

		if (c == 27)
			break;

		if (c == '1') {
			const char *str;
			unsigned char c=3;
			vga_write("Video mode? ");
			str = vga_gets(10);
			if (str) {
				c = atoi(str);
				int10_setmode(c);
				update_state_from_vga();
				if (c > 3) {
					do { c=getch(); } while (!(c == 13 || c == 10));
					int10_setmode(3);
					update_state_from_vga();
				}
			}
		}
		else if (c == 'A') {
			if (vga_flags & VGA_IS_AMSTRAD) {
				static unsigned char pat[4] = {0x11,0x55,0xEE,0xFF};
				static unsigned char shf[4] = {2,1,2,0};
				unsigned int x,y,c,b,s;
				VGA_RAM_PTR w;

				/* so apparently, you set up CGA 640x200 mono and then modify some bits */
				int10_setmode(6);
				outp(0x3D8,0x1B); /* graphics mode 2, graphics mode, 80-char mode for completeness, enable video display */
#if TARGET_MSDOS == 32
				vga_graphics_ram = (unsigned char*)0xB8000;
				vga_graphics_ram_fence = vga_graphics_ram + 0x8000;
#else
				vga_graphics_ram = (unsigned char far*)MK_FP(0xB800,0);
				vga_graphics_ram_fence = vga_graphics_ram + 0x8000;
#endif

				for (y=0;y < 200;y++) {
					w = vga_graphics_ram + ((y >> 1) * 80) + ((y & 1) * 0x2000);

					for (x=0;x < 80;x++) {
						c = (x >> 2) & 0xF;
						b = pat[x&3];
						s = (y & 3) * shf[x&3];
						b = (b << s) | (b >> (8 - s));
						outp(0x3DD,c ^ 0xF); w[x] = 0;
						outp(0x3DD,c);       w[x] = b;
					}
				}
				while (getch() != 13);
				int10_setmode(3);
			}
			else {
				vga_write("Only Amstrad may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'm') {
			int c;
			char tmp[256];
			unsigned char redraw=1;
			unsigned char disable=0;
			struct vga_mode_params mp,omp;
			vga_read_crtc_mode(&mp); omp = mp;

			do {
				if (redraw) {
					unsigned long rate;
					double hrate,vrate;

					rate = vga_clock_rates[mp.clock_select];
					if (mp.clock_div2) rate >>= 1;
					hrate = (double)rate / ((mp.clock9 ? 9 : 8) * mp.horizontal_total);
					vrate = hrate / mp.vertical_total;

					vga_clear();
					vga_moveto(0,0);
					vga_write_color(0x7);

					sprintf(tmp,
						"Clk=%u(%luHz) H%c V%c 9clk=%u div2=%u w/dw=%u/%u hr=%.3fKHz vr=%.3fHz\n",
						mp.clock_select,
						vga_clock_rates[mp.clock_select] >> (mp.clock_div2 ? 1 : 0),
						mp.hsync_neg?'-':'+',
						mp.vsync_neg?'-':'+',
						mp.clock9,
						mp.clock_div2,
						mp.word_mode,
						mp.dword_mode,
						hrate / 1000,
						vrate);
					vga_write(tmp);

					sprintf(tmp,
						"ref_cycles/scanline=%u inc_only_4th=%u\n",
						mp.refresh_cycles_per_scanline,
						mp.inc_mem_addr_only_every_4th);
					vga_write(tmp);

					sprintf(tmp,
						"V: total=%u s/e-rtrace=%u-%u de=%u s/e-blank=%u=%u\n",
						mp.vertical_total,
						mp.vertical_start_retrace,
						mp.vertical_end_retrace,
						mp.vertical_display_end,
						mp.vertical_blank_start,
						mp.vertical_blank_end);
					vga_write(tmp);

					sprintf(tmp,
						"H: total=%u s/e-rtrace=%u-%u de=%u s/e-blank=%u-%u del@tot=%u del@ret=%u\n",
						mp.horizontal_total,
						mp.horizontal_start_retrace,
						mp.horizontal_end_retrace,
						mp.horizontal_display_end,
						mp.horizontal_blank_start,
						mp.horizontal_blank_end,
						mp.horizontal_start_delay_after_total,
						mp.horizontal_start_delay_after_retrace);
					vga_write(tmp);

					sprintf(tmp,
						"SLR=%u shift4=%u aws=%u memadiv2=%u scandiv2=%u map14=%u map13=%u\n",
						mp.shift_load_rate,
						mp.shift4_enable,
						mp.address_wrap_select,
						mp.memaddr_div2,
						mp.scanline_div2,
						mp.map14,
						mp.map13);
					vga_write(tmp);

					vga_write("\n");
					vga_write("h/H htotal  v/V vtotal  c/C clock  9/9 8/9clk  2/2 div2  w/W word  d/D dword\n");
					vga_write("[/[ HS+/-   ]/] VS+/-\n");
				}
				redraw = 0;
				c = getch();
				if (c == 27) break;
				else if (c == '2') {
					mp.clock_div2 = !mp.clock_div2;
					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == '[') {
					mp.hsync_neg = !mp.hsync_neg;
					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == ']') {
					mp.vsync_neg = !mp.vsync_neg;
					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == '9' || c == '(') {
					mp.clock9 = !mp.clock9;
					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == 'w' || c == 'W') {
					mp.word_mode = (c == 'W');
					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == 'd' || c == 'D') {
					mp.dword_mode = (c == 'D');
					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == 'c' || c == 'C') {
					if (c == 'C') mp.clock_select++;
					else if (c == 'c') mp.clock_select--;
					mp.clock_select &= 3;
					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == 'v' || c == 'V') {
					if (c == 'v') {
						if (mp.vertical_total > 4)
							mp.vertical_total--;
					}
					else {
						if (mp.vertical_total < 1023)
							mp.vertical_total++;
					}

					/* recompute start/end retrace/blank in proportion to the new value */
					mp.vertical_start_retrace =
						(((long)mp.vertical_total * omp.vertical_start_retrace) / omp.vertical_total);
					mp.vertical_end_retrace = mp.vertical_start_retrace +
						(omp.vertical_end_retrace - omp.vertical_start_retrace);
					mp.vertical_blank_start =
						(((long)mp.vertical_total * omp.vertical_blank_start) / omp.vertical_total);
					mp.vertical_blank_end = mp.vertical_blank_start +
						(omp.vertical_blank_end - omp.vertical_blank_start);
					mp.vertical_display_end =
						(((long)mp.vertical_total * omp.vertical_display_end) / omp.vertical_total);

					/* experience points:
					 *
					 *    nVidia VGA seems to demand a minimum 2 clocks between horizontal_blank_end
					 *      and horizontal_total, or else it ends up blanking through the next line.
					 *
					 *    Intel VGA emulation hates this code especially when the output is the LVDS
					 *      laptop display :( */
					if ((mp.vertical_blank_end+2) > mp.vertical_total) {
						mp.vertical_blank_end = mp.vertical_total - 2;
						mp.vertical_blank_start = mp.vertical_blank_end -
							(omp.vertical_blank_end - omp.vertical_blank_start);
					}
					if (mp.vertical_blank_start < mp.vertical_display_end)
						mp.vertical_blank_start = mp.vertical_display_end;

					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == 'h' || c == 'H') {
					if (c == 'h') {
						if (mp.horizontal_total > 4)
							mp.horizontal_total--;
					}
					else {
						if (mp.horizontal_total < 255)
							mp.horizontal_total++;
					}

					/* recompute start/end retrace/blank in proportion to the new value */
					mp.horizontal_start_retrace =
						(((long)mp.horizontal_total * omp.horizontal_start_retrace) / omp.horizontal_total);
					mp.horizontal_end_retrace = mp.horizontal_start_retrace +
						(omp.horizontal_end_retrace - omp.horizontal_start_retrace);
					mp.horizontal_blank_start =
						(((long)mp.horizontal_total * omp.horizontal_blank_start) / omp.horizontal_total);
					mp.horizontal_blank_end = mp.horizontal_blank_start +
						(omp.horizontal_blank_end - omp.horizontal_blank_start);
					mp.horizontal_display_end =
						(((long)mp.horizontal_total * omp.horizontal_display_end) / omp.horizontal_total);

					/* experience points:
					 *
					 *    nVidia VGA seems to demand a minimum 2 clocks between horizontal_blank_end
					 *      and horizontal_total, or else it ends up blanking through the next line.
					 *
					 *    Intel VGA emulation hates this code especially when the output is the LVDS
					 *      laptop display :( */
					if ((mp.horizontal_blank_end+2) > mp.horizontal_total) {
						mp.horizontal_blank_end = mp.horizontal_total - 2;
						mp.horizontal_blank_start = mp.horizontal_blank_end -
							(omp.horizontal_blank_end - omp.horizontal_blank_start);
					}
					if (mp.horizontal_blank_start < mp.horizontal_display_end)
						mp.horizontal_blank_start = mp.horizontal_display_end;

					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == 'T') {
					if (!disable) {
						redraw=1;
						disable=1;
						int10_setmode(3);
						update_state_from_vga();
						if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
					}
				}
				else if (c == ' ') {
					if (!disable) {
						vga_correct_crtc_mode(&mp);
						vga_write_crtc_mode(&mp);
						redraw = 1;
					}
				}
				else if (c == 'r') {
					if (!disable) {
						vga_read_crtc_mode(&mp);
						redraw = 1;
					}
				}
			} while (1);
		}
		else if (c == 'x') {
/* FIXME: All this wonderful code overflows the 64KB code limit in Watcom C++ under Compact memory model */
#if TARGET_MSDOS == 16 && defined(__COMPACT__)
			/* NOTHING */
#else
			/* FIXME: Would these tests actually work on an EGA? */
			if (vga_flags & (VGA_IS_VGA|VGA_IS_EGA)) {
				VGA_RAM_PTR wr;
				VGA_ALPHA_PTR awr;
				unsigned int x,y,cc,sino=0,divpt,divpt2;
				unsigned char sine[128];
				unsigned int h;

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
				h = (vga_flags & VGA_IS_VGA) ? 400 : 350;

				/* -------------------------------------------------------------------------------- */
				/* test #1: does your VGA double-buffer the horizontal panning register?            */
				/* -------------------------------------------------------------------------------- */
				/* DOSBox 0.74:          yes  [rock solid]
				 * ATI Radeon:           no   [shimmering effect, cursor shimmers with text]
				 * nVidia Geforce:       yes  [rock solid]
				 * Intel 855GM:          yes  [rock solid]
				 */
				awr = vga_alpha_ram;
				for (y=0;y < (80*25);y++)
					awr[y] = 0x1E00 | 176;

				vga_moveto(0,0);
				vga_write_color(0x0E);
				vga_write("Horizontal PEL double-buffering test\n");
				vga_write("Some VGA chipsets do not double-buffer the horizontal pixel offset register\n");
				vga_write("If your screen is shimmering right now, then it's one of them");
				vga_write_sync();

				for (y=0;y < 128;y++)
					sine[y] = (unsigned char)((sin((double)y * 3.14159 / 16) * 4) + 4);
						
				do {
					_sti();
					if (kbhit()) {
						if (getch() == 27) break;
					}
					_cli();

					vga_wait_for_vsync();
					/* set offset to zero so that VGA chipsets that DO buffer it have a static screen */
					vga_set_xpan(vga_9wide ? 8 : 0);
					vga_wait_for_vsync_end();
					for (cc=0;cc < 12;cc++) { /* make sure we're far enough into the active area that our changes do not take effect immediately
								     on chipsets that DO double buffer */
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					/* now twiddle with the register */
					for (y=0;y < (h - 24);y++) {
						vga_wait_for_hsync();
						vga_set_xpan(sine[(y+sino)&127]);
						vga_wait_for_hsync_end();
					}
					vga_set_xpan(vga_9wide ? 8 : 0);
					sino++;
				} while(1);

				/* -------------------------------------------------------------------------------- */
				/* test #2: does your VGA double-buffer the max scan line register?                 */
				/* -------------------------------------------------------------------------------- */
				/* DOSBox 0.74:          ??  [every write to the register triggers a full screen redraw]
				 * ATI Radeon:           no  [screen warps]
				 * Intel:                no  [screen warps---very jumpy possibly due to Fujitsu BIOS]
				 */
				awr = vga_alpha_ram;
				for (y=0;y < (80*25);y++)
					awr[y] = 0x1E00 | 176;

				vga_moveto(0,0);
				vga_write_color(0x0E);
				vga_write("Max scan line register double-buffering test\n");
				vga_write("Some VGA chipsets do not double-buffer this register.\n");
				vga_write("If your screen is warping right now, then it's one of them");
				vga_write_sync();

				for (y=0;y < 128;y++) {
					sine[y] = (unsigned char)((sin((double)y * 3.14159 / 128) * 9) + 8);
					if (sine[y] & 0x80) sine[y] = 0;
				}
						
				do {
					_sti();
					if (kbhit()) {
						if (getch() == 27) break;
					}
					_cli();

					vga_wait_for_vsync();
					/* set correct count so that VGA chipsets that DO buffer it have a static screen */
					vga_write_CRTC(9,15);
					vga_wait_for_vsync_end();
					for (cc=0;cc < 12;cc++) { /* make sure we're far enough into the active area that our changes do not take effect immediately
								     on chipsets that DO double buffer */
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					/* now twiddle with the register */
					for (y=0;y < (h - 24);y++) {
						vga_wait_for_hsync();
						vga_write_CRTC(9,sine[(y+sino)&127]);
						vga_wait_for_hsync_end();
					}
					vga_write_CRTC(9,vga_flags & VGA_IS_VGA ? 15 : 13);
					sino++;
				} while(1);

				if (vga_flags & VGA_IS_VGA) {
					/* now do it in mode 19 */
					/* DOSBox 0.74:          no  [screen warps]
					 * ATI Radeon:           no  [screen warps]
					 * Intel:                no  [screen warps---very jumpy possibly due to Fujitsu BIOS]
					 */
					int10_setmode(19);
					update_state_from_vga();
					wr = vga_graphics_ram;
					for (y=0;y < 200;y++) {
						for (x=0;x < 320;x++) {
							*wr++ = y ^ x;
						}
					}

					{
						double err = 0,a;
						for (y=0;y < 128;y++) {
							a = sin((double)y * 3.14159 / 128) * 2;
							sine[y] = (unsigned char)((int)(a + err) + 1);
							err = a + err;
							err -= floor(err);
							if (sine[y] & 0x80) sine[y] = 0;
						}
					}

					do {
						_sti();
						if (kbhit()) {
							if (getch() == 27) break;
						}
						_cli();

						vga_wait_for_vsync();
						/* set correct count so that VGA chipsets that DO buffer it have a static screen */
						vga_write_CRTC(9,1);
						vga_wait_for_vsync_end();
						for (cc=0;cc < 12;cc++) { /* make sure we're far enough into the active area that our changes do not take effect immediately
									     on chipsets that DO double buffer */
							vga_wait_for_hsync();
							vga_wait_for_hsync_end();
						}
						/* now twiddle with the register */
						for (y=0;y < (h - 24);y++) {
							vga_wait_for_hsync();
							vga_write_CRTC(9,sine[(y+sino)&127]);
							vga_wait_for_hsync_end();
						}
						vga_write_CRTC(9,1);
						sino++;
					} while(1);
					/* restore mode 3 */
					int10_setmode(3);
					update_state_from_vga();
					if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
				}

				/* -------------------------------------------------------------------------------- */
				/* test #3: does your VGA double-buffer the line compare register (splitscreen)?    */
				/* -------------------------------------------------------------------------------- */
				/* VGA splitscreen happens when the line compare register equals the scan line of
				 * the raster. On a VGA that doesn't double-buffer, we can set up the screen as if
				 * splitscreen but then deliberately confuse it into not displaying the splitscreen.
				 * We do this by setting line compare to something (say, the middle of the screen)
				 * and then just before the raster gets there, setting line compare to a scan line
				 * prior to where the raster is. The VGA never has a chance to match the scan line
				 * and splitscreen never happens. */
				/* -------------------------------------------------------------------------------- */
				/* chipsets that double buffer line compare: */
				/* DOSBox 0.74:          yes [mirror image]
				 * ATI Radeon:           no  [no mirror]
				 * Intel:                no  [no mirror]
				 */
				awr = vga_alpha_ram;
				for (y=0;y < (80*25*2);y++) /* 2 h/w pages */
					awr[y] = 0x1E00 | 176;

				vga_moveto(0,0);
				vga_write_color(0x0E);
				vga_write("Line compare double-buffer test\n");
				vga_write("Most VGA chipsets do not double-buffer the line compare register\n");
				vga_write("If you can see the splitscreen (a mirror of this text halfway down)\n");
				vga_write("your chipset is NOT one of them.");
				vga_write_sync();
						
				do {
					_sti();
					if (kbhit()) {
						if (getch() == 27) break;
					}
					_cli();

					vga_wait_for_vsync();
					vga_splitscreen(h/2);
					vga_wait_for_vsync_end();
					for (cc=0;cc < ((h/2)-32);cc++) { /* wait until just before the scan line (give or take 32) */
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					vga_splitscreen(1); /* whoops! :) */
				} while(1);
				vga_splitscreen(0x3FF);

				/* ----------------------------- 2nd test ---------------------------- */
				/* if it doesn't double-buffer line compare, then we can probably trick
				 * it into splitting the screen (resetting to address 0) more than once,
				 * since the card is designed to do so when line count == line compare. */
				awr = vga_alpha_ram;
				for (y=0;y < (80*25*2);y++) /* 2 h/w pages */
					awr[y] = 0x1E00 | 176;

				vga_moveto(0,0);
				vga_write_color(0x0E);
				vga_write("Line compare double-buffer test II\n");
				vga_write("It follows that if the split happens when row counter == line compare,\n");
				vga_write("and line compare is NOT buffered, that we can modify the line compare during\n");
				vga_write("active display and trick the VGA into split-screening multiple times.");
				vga_write_sync();
						
				do {
					_sti();
					if (kbhit()) {
						if (getch() == 27) break;
					}
					_cli();

					vga_wait_for_vsync();
					vga_wait_for_vsync_end();
					vga_splitscreen(80);
					for (cc=0;cc < 84;cc++) { /* wait until just before the scan line (give or take 32) */
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					for (y=80+64;y < (h-8);y += 64) {
						vga_splitscreen(y);
						for (cc=0;cc < 64;cc++) { /* wait until just before the scan line (give or take 32) */
							vga_wait_for_hsync();
							vga_wait_for_hsync_end();
						}
					}
				} while(1);
				vga_splitscreen(0x3FF);

				/* -------------------------------------------------------------------------------- */
				/* test #4: does your VGA double-buffer the offset register (stride, line to line)? */
				/* -------------------------------------------------------------------------------- */
				/* chipsets that double buffer it: */
				/* DOSBox 0.74 [vgaonly]:       no
				 * DOSBox 0.74 [other vga]:     ?? [kind of... resolution is coarse about 1/4th the screen]
				 * DOSBox 0.74 [ega]:           yes
				 * ATI Radeon:                  no
				 * Intel:                       no
				 */
				awr = vga_alpha_ram;
				for (y=0;y < (80*25);y++) /* 2 h/w pages */
					awr[y] = 0x1E00 | 176;

				divpt2 = ((15+5) * ((vga_flags & VGA_IS_VGA) ? 16 : 14)) + 1; /* give the VGA chipset time to latch into the offset of the box */
				divpt = (15 * ((vga_flags & VGA_IS_VGA) ? 16 : 14)) + 1; /* give the VGA chipset time to latch into the offset of the box */
				vga_moveto(0,0);
				vga_write_color(0x0E);
				vga_write("Offset register double-buffer test\n");
				vga_write("The offset register determines the number of bytes per scan line.\n");
				vga_write("Most VGA chipsets do not buffer this register, making it possible\n");
				vga_write("to change bytes/line mid-screen.\n");
				vga_write("\n");
				vga_write("If you can see the box below whole, then your chipset is one of them\n");

				vga_moveto(0,14);
				vga_write_color(0x0A);
				vga_write("This text should be on the left side, and the box should show up below:\n");
				vga_write_sync();
				{
					unsigned int ofs = 80*15;
					for (y=16;y <= 18;y++) {
						awr[ofs+((y-15)*120)] = 0x0E00 | 186;
						awr[ofs+((y-15)*120)+22] = 0x0E00 | 186;
					}
					for (x=1;x <= 21;x++)
						awr[ofs+x] = 0x0E00 | 205;
					for (x=1;x <= 21;x++)
						awr[ofs+x+(4*120)] = 0x0E00 | 205;
					awr[ofs+((0*120)+0)] = 0x0E00 | 201;
					awr[ofs+((0*120)+22)] = 0x0E00 | 187;
					awr[ofs+((4*120)+0)] = 0x0E00 | 200;
					awr[ofs+((4*120)+22)] = 0x0E00 | 188;
				}
				{
					unsigned int ofs = (80*15)+(120*5);
					const char *msg = "This text should be on the left side, and repeating";
					VGA_ALPHA_PTR w = vga_alpha_ram + ofs;
					while (*msg) *w++ = 0x0A00 | *msg++;
				}
				{
					unsigned int ofs = (80*15)+(120*5)+(80*1);
					const char *msg = "You should not be able to see this text";
					VGA_ALPHA_PTR w = vga_alpha_ram + ofs;
					while (*msg) *w++ = 0x0C00 | *msg++;
				}

				do {
					_sti();
					if (kbhit()) {
						if (getch() == 27) break;
					}
					_cli();

					vga_wait_for_vsync();
					vga_write_CRTC(0x13,80 / 2);	/* byte mode, right? */
					vga_wait_for_vsync_end();
					for (cc=0;cc < divpt;cc++) { /* wait until the text line prior to where want to change */
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					/* change the stride */
					vga_write_CRTC(0x13,120 / 2);	/* byte mode, right? */
					for (;cc < divpt2;cc++) { /* wait until the text line prior to where want to change */
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					vga_write_CRTC(0x13,0 / 2);	/* byte mode, right? */
				} while(1);
				vga_write_CRTC(0x13,80 / 2);	/* word mode, right? */

				/* FIXME: Windows 98 bootdisk + Bochs 2.4.6: Emulator hangs at this point */

				{
					unsigned char blobs[14*2],bdir[14];
					unsigned char check1=0,check2=0,c1a,counter;
					/* -------------------------------------------------------------------------------- */
					/* test #5: if I set stride == 0, can I write over the VRAM mid-scan to fake a picture */
					/* -------------------------------------------------------------------------------- */
					/* [another common trick by old demos: set stride == 0 and then once per scanline
					 * change the VRAM contents. This is usually associated with a moving checkerboard and
					 * a "3D" moving platform that, on older hardware, would normally be impossible to draw
					 * realtime. This is usually done in VGA graphics modes. We're probably the first to
					 * try it in alphanumeric text mode :) */
					vga_write_CRTC(9,0);
					vga_write_CRTC(0x13,0 / 2);	/* byte mode, right? */

					/* also reprogram character ram to turn chars into 1:1 mapping with bits */
					vga_alpha_switch_to_font_plane();
					for (x=0;x < 256;x++) vga_graphics_ram[x*32] = x;
					vga_alpha_switch_from_font_plane();
					if (vga_flags & VGA_IS_VGA) vga_set_9wide(0);

					for (y=0;y < 14;y++) {
						blobs[y*2 + 0] = rand()%80;
						blobs[y*2 + 1] = rand()%256;
						bdir[y] = ((rand()&1)*2) - 1;
					}

					counter = 0;
					do {
						_sti();
						if (kbhit()) {
							if (getch() == 27) break;
						}
						_cli();

						vga_wait_for_vsync();
						vga_wait_for_vsync_end();
						awr = vga_alpha_ram;
						for (y=0;y < 80;y++) awr[y] = 0;
						for (cc=0;cc < 4;cc++) {
							vga_wait_for_hsync();
							vga_wait_for_hsync_end();
						}
						for (cc=0;cc < 4;cc++) {
							vga_wait_for_hsync();
							for (y=0;y < 80;y++) awr[y] = (0x55 << (cc&1)) | 0x0800;
							vga_wait_for_hsync_end();
						}
						for (y=0;y < 80;y++) awr[y] = 0;
						for (cc=0;cc < 2;cc++) {
							vga_wait_for_hsync();
							vga_wait_for_hsync_end();
						}

						/* checkerfield, and "blobs" */
						{
							unsigned short a=0x07FF,b=0;
							for (cc=0;cc < 80;cc += 4) {
								awr[cc+0] = a;
								awr[cc+1] = a;
								awr[cc+2] = b;
								awr[cc+3] = b;
							}
						}

						c1a=check1;
						for (y=0;y < (h-32);y++) {
							vga_wait_for_hsync_end();
							for (cc=0;cc < 14;cc++) {
								unsigned char cmp = blobs[cc*2 + 1];
								if (y == cmp) {
									awr[blobs[cc*2]] = ((cc+1) << 8) | 0xFF;
								}
							}

							vga_wait_for_hsync();
							c1a++;
							if ((c1a & 15) == 0 || y == 0) {
								vga_write_AC(0,(c1a & 16) ? 7 : 0);
								vga_write_AC(7,(c1a & 16) ? 0 : 7);
								vga_AC_reenable_screen();
							}
						}

						vga_write_AC(0,0);
						vga_write_AC(7,7);
						vga_AC_reenable_screen();
						for (cc=0;cc < 80;cc++) awr[cc] = 0;
						for (cc=0;cc < 14;cc++) {
							blobs[cc*2] += bdir[cc];
							if ((signed char)blobs[cc*2] < 0) {
								blobs[cc*2] = 0;
								bdir[cc] = 1;
							}
							else if (blobs[cc*2] >= 80) {
								blobs[cc*2] = 79;
								bdir[cc] = -1;
							}
						}

						counter++;
						if ((counter & 1) == 1) check1++;
						check2++;
					} while(1);
					vga_write_CRTC(9,vga_flags & VGA_IS_VGA ? 15 : 13);
					vga_write_CRTC(0x13,80 / 2);	/* word mode, right? */
				}

				int10_setmode(3);
				update_state_from_vga();
			}
			else {
				vga_write("Only VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
#endif
		}
		else if (c == '9') {
			if (vga_flags & VGA_IS_VGA) {
				if (!vga_alpha_mode) {
					int10_setmode(3);
					update_state_from_vga();
				}

				vga_set_9wide(!vga_9wide);
				update_state_from_vga();
				vga_want_9wide = vga_9wide;
			}
			else {
				vga_write("Only VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'F') { /* in this test we fuck around with EGA/VGA character RAM */
			if (vga_flags & (VGA_IS_VGA|VGA_IS_EGA)) {
				unsigned int lineheight = (vga_flags & VGA_IS_VGA) ? 16 : 14;
				unsigned int x,y;
				VGA_ALPHA_PTR wr;
				VGA_RAM_PTR fr;

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);

				wr = vga_alpha_ram;
				for (x=0;x < (80*25);x++) {
					wr[x] = 0x400 + x;
				}
				while (getch() != 13);

				vga_alpha_switch_to_font_plane();
				for (x=0;x < 16;x++) {
					fr = vga_graphics_ram + (x*32);
					for (y=0;y < lineheight;y++) {
						vga_wait_for_vsync();
						fr[y] = 0x55 << (y&1);
						vga_wait_for_vsync_end();
					}
				}
				for (x=127;x < 256;x++) {
					fr = vga_graphics_ram + (x*32);
					for (y=0;y < lineheight;y++) {
						fr[y] = 0x55 << (y&1);
					}
				}
				vga_alpha_switch_from_font_plane();
				wr = vga_alpha_ram;
				{
					const char *msg = "TADAAAAaaaa!";
					while (*msg) *wr++ = ((unsigned char)(*msg++)) | 0x1A00;
				}
				while (getch() != 13);
				vga_alpha_switch_to_font_plane();
				fr = vga_graphics_ram;
				for (x=0;x < (256*32);x++) {
					/* NTS: XOR and then NOT, make them different to discourage the compiler from optimizing them out */
					*fr ^= 0xFF;
					*fr = ~(*fr);
				}
				vga_alpha_switch_from_font_plane();
				while (getch() != 13);

				/* read and copy back the font----make sure we're reading it correctly */

				/* now play with the 512-code mode */
				/* NTS: Nobody seems to mention this, but if the bitfields were extended from EGA in the
				 *      exact same manner that would imply the EGA also has the ability to do 512-char
				 *      mode! Right? Perhaps someday I'll find some EGA hardware to verify this on */
				{
					vga_write_CRTC(9,7); /* transition to 80x50 */
					wr = vga_alpha_ram;
					for (y=0;y < 50;y++) {
						for (x=0;x < 80;x++) {
							*wr++ = (x + 16) | ((y&1) << 11) | (((y>>1)&7) << 8) | ((y>>4) << 12);
						}
					}

					vga_alpha_switch_to_font_plane();
					/* FIXME: What's with the FONT CORRUPTION seen on actual ATI & Intel hardware here?? */
					/* in-place convert 8x16 -> 8x8 */
					for (x=0;x < 256;x++) {
						unsigned char a,b;
						fr = vga_graphics_ram + (x*32);
						for (y=1;y < 8;y++) {
							a = vga_force_read(fr+(y<<1));
							b = vga_force_read(fr+(y<<1)+1);
							vga_force_write(fr+y,a|b);
						}
					}
					/* then make the 2nd set a mirror image */
					fr = vga_graphics_ram;
					for (x=0;x < (256*32);x++) {
						unsigned char c=0,i=fr[x];
						for (y=0;y < 8;y++) {
							if (i & (1 << y))
								c |= 128 >> y;
						}
						fr[x+0x2000] = c;
					}
					for (x=0;x < (256*32);x++) {
						fr[(x^7)+0x4000] = fr[x+0x2000];
					}
					for (x=0;x < (256*32);x++) {
						fr[(x^7)+0x6000] = fr[x];
					}
					vga_alpha_switch_from_font_plane();
					while (getch() != 13);
					vga_select_charset_a_b(0,0x2000);
					while (getch() != 13);
					vga_select_charset_a_b(0,0x4000);
					while (getch() != 13);
					vga_select_charset_a_b(0,0x6000);
					while (getch() != 13);
					for (x=8;x < 16;x++) vga_write_AC(x,x&7);
					vga_AC_reenable_screen();
					while (getch() != 13);
					/* play with the underline location reg */
					vga_write_CRTC(0x14,(vga_read_CRTC(0x14) & 0xE0) | 7);
					while (getch() != 13);
					for (y=6;1;) {
						vga_wait_for_vsync_end();
						vga_wait_for_vsync();
						for (x=0;x < 32;x++) {
							vga_wait_for_hsync();
							vga_wait_for_hsync_end();
						}
						vga_write_CRTC(0x14,(vga_read_CRTC(0x14) & 0xE0) | y);
						if (y == 0) break;
						y--;
					}
					while (getch() != 13);
				}

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only EGA/VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 's') {
			if (vga_flags & (VGA_IS_VGA|VGA_IS_EGA)) {
				unsigned int lineheight = (vga_flags & VGA_IS_VGA) ? 16 : 14;
				unsigned int y,h = (vga_flags & VGA_IS_VGA) ? 400 : 350,v,cc;
				unsigned int charw = 8;
				unsigned int yofs;
				VGA_ALPHA_PTR wr;

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA) {
					if (vga_want_9wide != 0xFF) {
						vga_set_9wide(vga_want_9wide);
						charw = vga_want_9wide ? 9 : 8;
					}
					else {
						charw = vga_9wide ? 9 : 8;
					}
				}

				/* NTS: everyone knows the VGA holds on to your register updates until vertical retrace, but
				 *      when exactly after retrace they take effect seems to wildly vary between VGA cards.
				 *      to ensure our new values are latched on time, we delay (using HSYNC) 32 scanlines
				 *      so that our new values are written at a time when we're well into the active picture
				 *      and the CRTC would have to be WAY out of compliance to have not updated itself by then!
				 *
				 *      DOSBox 0.74 [vgaonly]:         a good 8-10 scanlines after vblank, or else ypan is jumpy
				 *      ATI:                           ypan takes effect, hpel might or might not [jumpy horz. scrolling]
				 *      NVidia:                        a good 8-10 scanlines, or else ypan is jumpy
				 *      Intel (855/915/945/etc)        right after vblank [perfect scrolling] */

				vga_set_start_location(80*25); /* show 2nd page */
				wr = vga_alpha_ram + (80*25);
				{ /* draw pattern on 2nd page */
					for (y=0;y < (80*25);y++) {
						wr[y] = y + 0x200;
					}
				}
				{ /* draw message on first page */
					const char *msg = "EGA/VGA Split-screen!";
					wr = vga_alpha_ram;
					while (*msg) *wr++ = (unsigned char)(*msg++) | 0x1E00;
					while (wr < (vga_alpha_ram+(80*25))) *wr++ = 0x1FB1;
				}
				y = 0; v = 1;
				do { /* loop */
					if (kbhit()) {
						if (getch() == 27) break;
					}

					/* NTS: ATI and Intel VGA do not double-buffer the splitscreen register.
					 *      Updating it during the active display area would cause a visible jump
					 *      and possible flash whenever the split point crossed our 32-scanline
					 *      wait loop. So we need to wait until VBLANK to update it */
					_cli();
					vga_wait_for_vsync_end();
					for (cc=0;cc < 32;cc++) {
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					vga_wait_for_vsync();
					vga_splitscreen(y);
					vga_wait_for_vsync_end();
					_sti();
					y += v;
					if (y == 0) v = 1;
					else if (y == (h+5)) v = -1;
				} while (1);

				y = 0; v = 1;
				do { /* loop */
					if (kbhit()) {
						if (getch() == 27) break;
					}

					/* NTS: ATI chipsets DO double-buffer the offset register and ypan (as expected by the VGA
					 *      standard) but DO NOT double-buffer the horizontal pel register. Same comments apply
					 *      regarding the splitscreen register */
					_cli();
					vga_wait_for_vsync_end();
					for (cc=0;cc < 32;cc++) {
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					yofs = h - y;
					/* NTS: offset and ypan need to be updated during active area, for CRTC to latch in on VBLANK */
					vga_set_start_location(80*25 + ((yofs/lineheight)*80)); /* show 2nd page */
					vga_set_ypan_sub(yofs%lineheight);
					vga_wait_for_vsync();
					/* NTS: splitscreen register needs to be updated DURING VBLANK because some chipsets (ATI) do not buffer them */
					vga_splitscreen(y);
					vga_wait_for_vsync_end();
					y += v;
					if (y == 0) v = 1;
					else if (y == (h+1)) v = -1;
					_sti();
				} while (1);

				y = 0; v = 1;
				if (vga_flags & VGA_IS_VGA)
					vga_write_AC(0x10|0x20,vga_read_AC(0x10) | 0x20); /* disable hpen on splitscreen */
				else
					vga_write_AC(0x10|0x20,0x20); /* disable hpen on splitscreen */
				
				do { /* loop */
					if (kbhit()) {
						if (getch() == 27) break;
					}

					/* NTS: ATI chipsets DO double-buffer the offset register and ypan (as expected by the VGA
					 *      standard) but DO NOT double-buffer the horizontal pel register. Same comments apply
					 *      regarding the splitscreen register */
					_cli();
					vga_wait_for_vsync_end();
					for (cc=0;cc < 32;cc++) {
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					yofs = h - y;
					/* NTS: ATI chipsets DO buffer ypan and offset registers but DO NOT buffer horizontal pel
					 *      and splitscreen */
					vga_set_start_location(80*25 + ((yofs/lineheight)*80) + (yofs/charw)); /* show 2nd page */
					vga_set_ypan_sub(yofs%lineheight);
					vga_wait_for_vsync();
					/* NTS: splitscreen & hpel need to be updated DURING VBLANK because some chipsets (ATI) do not buffer them */
					vga_splitscreen(y);
					if (charw == 9) vga_set_xpan((yofs+8)%charw); /* apparently in 9-bit mode it's off by 1 and 8 -> 0 */
					else		vga_set_xpan(yofs%charw);
					vga_wait_for_vsync_end();
					y += v;
					if (y == 0) v = 1;
					else if (y == (h+1)) v = -1;
					_sti();
				} while (1);

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only EGA/VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'S') {
			if (vga_flags & VGA_IS_EGA) {
				uint16_t color16;
				unsigned char mode,c;
				volatile VGA_RAM_PTR wr;
				unsigned int w,h,x,y,v,rv;

				w = 640;
				if (vga_flags & VGA_IS_VGA) {
					mode = 18;
					h = 480;
				}
				else {
					mode = 16;
					h = 350;
				}

				int10_setmode(mode);
				update_state_from_vga();
				vga_write_sequencer(VGA_SC_MAP_MASK,	0xF);	/* map mask register = enable all planes */
				vga_write_GC(VGA_GC_ENABLE_SET_RESET,	0x00);	/* enable set/reset = no on all planes */
				vga_write_GC(VGA_GC_SET_RESET,		0x00);	/* set/reset register = all zero */
				vga_write_GC(VGA_GC_BIT_MASK,		0xFF);	/* all bits modified */

				vga_write_GC(VGA_GC_DATA_ROTATE,	0 | VGA_GC_DATA_ROTATE_OP_NONE); /* rotate=0 op=unmodified */
				vga_write_GC(VGA_GC_MODE,		0x02);	/* 256=0 CGAstyle=0 odd/even=0 readmode=0 writemode=2 (copy CPU bits to each plane) */
				{
					wr = vga_graphics_ram;
					for (y=0;y < h;y++) {
						color16 = (y>>3) * 0x101;
						/* NTS: performance hack: issue 16-bit memory I/O */
						for (x=0;x < 64;x += 16) {
							*((VGA_ALPHA_PTR)wr) = (uint16_t)color16;
							wr += 2;
						}
						wr += (w-64)/8;
					}
				}

				/* play with attribute controller */
				for (c=0;c <  4;c++) vga_write_AC(c,vga_AC_RGB_to_code(c,c,c));
				for (   ;c <  7;c++) vga_write_AC(c,vga_AC_RGB_to_code(c-3,0,0));
				for (   ;c < 10;c++) vga_write_AC(c,vga_AC_RGB_to_code(c-6,c-6,0));
				for (   ;c < 13;c++) vga_write_AC(c,vga_AC_RGB_to_code(0,c-9,0));
				for (   ;c < 16;c++) vga_write_AC(c,vga_AC_RGB_to_code(0,0,c-12));
				vga_AC_reenable_screen();

				vga_wait_for_vsync();
				vga_wait_for_vsync_end();
				for (y=0;y < h;y++) {
					vga_wait_for_hsync();
					if (vga_in_vsync()) break;
					vga_wait_for_hsync_end();
				}
				if (h > y) h = y;
				if (h > 24) h -= 24;

				/* NTS: Apparently on Intel chipsets the timing isn't granular enough and the I/O is slow enough
				 *      that on the right-hand side of the screen you can see a flickering black band followed by
				 *      brief junk data and then the picture. That's what IBM gets for making EGA "palette registers"
				 *      that blank the screen when you write them-hah! [seen on: Intel 855GM/Fujitsu laptop, but only
				 *      when displayed on the internal LCD display. External VGA does not show this glitch] */

				x = 0; v = 1;
				do {
					vga_wait_for_vsync();
					/* TODO: When you get your keyboard code finished, poll the keyboard controller directly. Do NOT enable interrupts.
					 *       Hopefully never enabling interrupts resolves the horrible "pulsating" I'm seeing on one Fujitsu laptop I own */
					_sti();
					if (kbhit()) {
						if (getch() == 13) break;
					}
					_cli();
					vga_wait_for_vsync_end();

					rv = x;
					for (y=0;y < h;y++,rv++) {
						vga_wait_for_hsync();
						vga_write_AC(0,vga_AC_RGB_to_code((rv>>3)&3,(rv>>5)&3,(rv>>7)&3));
						vga_AC_reenable_screen();
						vga_wait_for_hsync_end();
					}

					x += v;
					if (x == 0) v = 1;
					else if (x == 392) v = -1;
				} while (1);
				_sti();

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only EGA/VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'R') {
			if (vga_flags & VGA_IS_VGA) {
				signed char tr,tg,tb,cc,cd;
				unsigned char r,g,b,*pal;
				volatile VGA_RAM_PTR wr;
				unsigned int w,h,x,y,v;
				int iy;

				int10_setmode(19);
				tr=63;tg=63;tb=63;cc=0;cd=1;
				update_state_from_vga();
				vga_enable_256color_modex();
				w = 320; h = (0x10000 / (w/4));
				x = 0; v = 1;

				wr = vga_graphics_ram;
				for (y=0;y < h;y++) {
					*wr++ = y;
					*wr++ = y;
					*wr++ = y;
					*wr++ = (y&1)?15:0;
					/* NTS: Some chipsets change how they interpret RAM depending on whether we
					 *      enable "Mode X" especially Intel chipsets */
					for (x=16;x < w;x += 4) *wr++ = 0;
				}

				vga_wait_for_vsync();
				vga_wait_for_vsync_end();
				for (y=0;y < h;y++) {
					vga_wait_for_hsync();
					if (vga_in_vsync()) break;
					vga_wait_for_hsync_end();
				}
				if (y > 392) y = 392;
				h = y;
				if (h > 24) h -= 24;

				do {
					vga_wait_for_vsync();
					/* TODO: When you get your keyboard code finished, poll the keyboard controller directly. Do NOT enable interrupts.
					 *       Hopefully never enabling interrupts resolves the horrible "pulsating" I'm seeing on one Fujitsu laptop I own */
					_sti();
					if (kbhit()) {
						if (getch() == 13) break;
					}
					_cli();

					pal = paltmp;
					for (y=0;y < h;y++) {
						iy = 64 - abs((int)y - (int)x); if (iy < 0) iy = 0;
						*pal++ = (tr*iy)>>6;
						*pal++ = (tg*iy)>>6;
						*pal++ = (tb*iy)>>6;
					}

					vga_wait_for_vsync_end();
					pal = paltmp;
					for (y=0;y < h;y++) {
						r = *pal++;
						g = *pal++;
						b = *pal++;
						vga_wait_for_hsync();
						vga_palette_lseek(0);
						vga_palette_write(r,g,b);
						vga_palette_lseek(15);
						vga_palette_write(63-r,63-g,63-b);
						vga_wait_for_hsync_end();
					}

					switch (cc) {
						case 0:
							tr += cd;
							if (tr > 63) { tr=63; cd = -1; cc++; }
							else if (tr < 0) { tr=0; cd = 1; cc++; }
							break;
						case 1:
							tg += cd;
							if (tg > 63) { tg=63; cd = -1; cc++; }
							else if (tg < 0) { tg=0; cd = 1; cc++; }
							break;
						case 2:
							tb += cd;
							if (tb > 63) { tb=63; cd = -1; cc++; }
							else if (tb < 0) { tb=0; cd = 1; cc++; }
							break;
						default:
							cc = 0;
							break;
					}

					x += v;
					if (x == 0) v = 1;
					else if (x == 392) v = -1;
				} while (1);
				_sti();

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'Y') {
			if (vga_flags & VGA_IS_VGA) {
				volatile VGA_RAM_PTR wr;
				unsigned int w,h,x,y,v,ii;

				int10_setmode(19);
				update_state_from_vga();
				vga_enable_256color_modex();
				w = 320; h = (0x10000 / (w/4));

				wr = vga_graphics_ram;
				for (y=0;y < h;y++) {
					*wr++ = y;
					*wr++ = y;
					*wr++ = y;
					*wr++ = (y&1)?15:0;
					wr += (w/4)-4;
				}

				for (x=16;x < w;x++) {
					vga_write_sequencer(VGA_SC_MAP_MASK,1 << (x&3));
					wr = vga_graphics_ram + (x>>2);
					for (y=0;y < h;y++) {
						*wr++ = (x^y);
						wr += (w/4)-1;
					}
				}
				while (getch() != 13);

				for (y=0;y < (h-200);y++) {
					if (kbhit()) {
						int c = getch();
						if (c == ' ' || c == 27) break;
					}

					vga_wait_for_vsync();
					vga_wait_for_vsync_end();
					for (ii=0;ii < 32;ii++) {
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					vga_set_start_location(y*(w/4));
				}
				while (getch() != 13);

				vga_palette_lseek(0);
				for (x=0;x < 64;x++) vga_palette_write(x,x,x);
				for (x=0;x < 64;x++) vga_palette_write(x,0,0);
				for (x=0;x < 64;x++) vga_palette_write(0,x,0);
				for (x=0;x < 64;x++) vga_palette_write(0,0,x);
				while (getch() != 13);

				for (y=31;1;) {
					vga_wait_for_vsync();
					vga_palette_lseek(0);
					for (x=0;x < 64;x++) {
						v = (x*y)>>5;
						vga_palette_write(v,v,v);
					}
					vga_wait_for_vsync_end();
					if (y-- == 0) break;
				}
				for (y=31;1;) {
					vga_wait_for_vsync();
					vga_palette_lseek(64);
					for (x=0;x < 64;x++) {
						v = (x*y)>>5;
						vga_palette_write(v,0,0);
					}
					vga_wait_for_vsync_end();
					if (y-- == 0) break;
				}
				for (y=31;1;) {
					vga_wait_for_vsync();
					vga_palette_lseek(128);
					for (x=0;x < 64;x++) {
						v = (x*y)>>5;
						vga_palette_write(0,v,0);
					}
					vga_wait_for_vsync_end();
					if (y-- == 0) break;
				}
				for (y=31;1;) {
					vga_wait_for_vsync();
					vga_palette_lseek(192);
					for (x=0;x < 64;x++) {
						v = (x*y)>>5;
						vga_palette_write(0,0,v);
					}
					vga_wait_for_vsync_end();
					if (y-- == 0) break;
				}
				while (getch() != 13);

				/* now play with panning */
				w = 640; h = 400;
				vga_set_start_location(0);
				vga_set_stride(w);
				for (x=0;x < w;x++) {
					vga_write_sequencer(VGA_SC_MAP_MASK,1 << (x&3));
					wr = vga_graphics_ram + (x>>2);
					for (y=0;y < h;y++) {
						*wr++ = (x^y);
						wr += (w/4)-1;
					}
				}
				vga_write_sequencer(VGA_SC_MAP_MASK,0xF);
				for (y=128;y < h;y += 56) {
					wr = vga_graphics_ram + ((w>>2)*y);
					for (x=0;x < w;x += 4) *wr++ = 63;
				}
				vga_write_sequencer(VGA_SC_MAP_MASK,0x8);
				for (x=256;x < w;x += 56) {
					wr = vga_graphics_ram + (x>>2);
					for (y=0;y < h;y++) {
						*wr++ = 63;
						wr += (w/4)-1;
					}
				}
				vga_palette_lseek(0);
				for (x=0;x < 64;x++) vga_palette_write(x,x,x);
				for (x=0;x < 64;x++) vga_palette_write(x,0,0);
				for (x=0;x < 64;x++) vga_palette_write(0,x,0);
				for (x=0;x < 64;x++) vga_palette_write(0,0,x);

				x = y = 0;
				v = 0;
				do {
					if (kbhit()) {
						int c = getch();
						if (c == 27) break;
					}

					switch (v) {
						case 0:	if (++x == (w-320)) v++; break;
						case 1: if (++y == (h-200)) v++; break;
						case 2: if (--x == 0) v++; break;
						case 3: if (--y == 0) v=0; break;
					}

					_cli();
					/* NTS: The CRTC double-buffers the offset register and updates at VBLANK
					 *      but some chipsets (ATI) don't double-buffer the hpel. If we don't
					 *      watch out for that our smooth pan will turn into a jumpy picture */
					/* So: Wait until raster scan is into the active area, then write offset register */
					vga_wait_for_vsync_end();
					for (ii=0;ii < 32;ii++) {
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					vga_set_start_location((y*(w/4))+(x>>2));
					/* Then wait for VBLANK and update hpel */
					vga_wait_for_vsync();
					vga_set_xpan((x&3)<<1);
					_sti();
				} while (1);

				vga_write_CRTC(0x09,0);
				x = y = 0;
				v = 0;
				do {
					if (kbhit()) {
						int c = getch();
						if (c == 27) break;
					}

					switch (v) {
						case 0:	if (++x == (w-320)) v += 2; break;
						case 2: if (--x == 0) v = 0; break;
					}

					_cli();
					vga_wait_for_vsync_end();
					for (ii=0;ii < 32;ii++) {
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					vga_set_start_location((y*(w/4))+(x>>2));
					vga_wait_for_vsync();
					vga_set_xpan((x&3)<<1);
					_sti();
				} while (1);

				x = 0; v = 0;
				vga_set_xpan(0);
				vga_set_start_location(0);
				do {
					if (kbhit()) {
						int c = getch();
						if (c == 27) break;
					}

					switch (v) {
						case 0: if (++x == 31) v++; break;
						case 1: if (--x == 0) v=0; break;
					}

					vga_wait_for_vsync();
					vga_wait_for_vsync_end();
					for (ii=0;ii < 32;ii++) {
						vga_wait_for_hsync();
						vga_wait_for_hsync_end();
					}
					vga_write_CRTC(9,x);
				} while (1);

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'X') {
			if (vga_flags & (VGA_IS_VGA|VGA_IS_MCGA)) {
				volatile VGA_RAM_PTR wr;
				unsigned int w,h,x,y;

				int10_setmode(19);
				update_state_from_vga();

				w = 320; h = 200;
				wr = vga_graphics_ram;

				for (y=0;y < (h/4);y++) {
					for (x=0;x < w;x++)
						*wr++ = x;
				}
				for (;y < h;y++) {
					for (x=0;x < w;x++)
						*wr++ = (x^y);
				}
				while (getch() != 13);

				vga_palette_lseek(0);
				for (c=0;c < 64;c++) vga_palette_write(c,c,c);
				for (c=0;c < 64;c++) vga_palette_write(c,0,0);
				for (c=0;c < 64;c++) vga_palette_write(0,c,0);
				for (c=0;c < 64;c++) vga_palette_write(0,0,c);
				while (getch() != 13);

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only VGA/MCGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'W') {
			if (vga_flags & VGA_IS_EGA) {
				uint16_t color16;
				unsigned char mode,c;
				unsigned int w,h,x,y;
				volatile VGA_RAM_PTR wr;

				w = 640;
				if (vga_flags & VGA_IS_VGA) {
					mode = 18;
					h = 480;
				}
				else {
					mode = 16;
					h = 350;
				}

				int10_setmode(mode);
				update_state_from_vga();
				vga_write_sequencer(VGA_SC_MAP_MASK,	0xF);	/* map mask register = enable all planes */
				vga_write_GC(VGA_GC_ENABLE_SET_RESET,	0x00);	/* enable set/reset = no on all planes */
				vga_write_GC(VGA_GC_SET_RESET,		0x00);	/* set/reset register = all zero */
				vga_write_GC(VGA_GC_BIT_MASK,		0xFF);	/* all bits modified */

				vga_write_GC(VGA_GC_DATA_ROTATE,	0 | VGA_GC_DATA_ROTATE_OP_NONE); /* rotate=0 op=unmodified */
				vga_write_GC(VGA_GC_MODE,		0x02);	/* 256=0 CGAstyle=0 odd/even=0 readmode=0 writemode=2 (copy CPU bits to each plane) */
				{
					wr = vga_graphics_ram;
					for (y=0;y < h;y++) {
						color16 = (y>>3) * 0x101;
						/* NTS: performance hack: issue 16-bit memory I/O */
						for (x=0;x < w;x += 16) {
							*((VGA_ALPHA_PTR)wr) = (uint16_t)color16;
							wr += 2;
						}
					}
				}
				while (getch() != 13);

				/* play with attribute controller */
				for (c=0;c <  4;c++) vga_write_AC(c,vga_AC_RGB_to_code(c,c,c));
				for (   ;c <  7;c++) vga_write_AC(c,vga_AC_RGB_to_code(c-3,0,0));
				for (   ;c < 10;c++) vga_write_AC(c,vga_AC_RGB_to_code(c-6,c-6,0));
				for (   ;c < 13;c++) vga_write_AC(c,vga_AC_RGB_to_code(0,c-9,0));
				for (   ;c < 16;c++) vga_write_AC(c,vga_AC_RGB_to_code(0,0,c-12));
				vga_AC_reenable_screen();
				while (getch() != 13);

				/* VGA only: play with the color palette registers */
				if (vga_flags & VGA_IS_VGA) {
					for (c=0;c < 16;c++) vga_write_AC(c,c);
					vga_AC_reenable_screen();
					vga_palette_lseek(0);
					for (c=0;c < 16;c++) {
						x = ((unsigned int)c * 63) / 15;
						vga_palette_write(x,x,x);
					}
					while (getch() != 13);

					vga_palette_lseek(0);
					for (c=0;c < 16;c++) {
						x = ((unsigned int)c * 63) / 15;
						vga_palette_write(x,0,0);
					}
					while (getch() != 13);

					vga_palette_lseek(0);
					for (c=0;c < 16;c++) {
						x = ((unsigned int)c * 63) / 15;
						vga_palette_write(0,x,0);
					}
					while (getch() != 13);

					vga_palette_lseek(0);
					for (c=0;c < 16;c++) {
						x = ((unsigned int)c * 63) / 15;
						vga_palette_write(0,0,x);
					}
					while (getch() != 13);
				}

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only EGA/VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}

		}
		else if (c == 'V') {
			if (vga_flags & VGA_IS_EGA) {
				uint16_t color16;
				unsigned char mode,color;
				unsigned int w,h,x,y;
				volatile VGA_RAM_PTR wr;

				/* FIXME: 
				 *        Emulator: Oracle VirtualBox VGA emulation
				 *             Env: 32-BIT flat MS-DOS
				 *         Problem: Seems to get stuck after drawing the first test pattern
				 *
				 *         Also related to:
				 *
				 *        Emulator: Microsoft Virtual PC
				 *         Problem: DOS32A complains that INT 43H was modified (only after running this routine)
				 */

				for (mode=13;mode <= 18;mode++) {
					if (mode == 15 || mode == 17) continue; /* skip mono modes */
					if (mode == 18 && (vga_flags & VGA_IS_VGA) == 0) continue; /* skip 640x480x16 if not VGA */

					if (mode == 13)
						w = 320;
					else
						w = 640;

					if (mode >= 17)
						h = 480;
					else if (mode >= 15)
						h = 350;
					else
						h = 200;

					int10_setmode(mode);
					update_state_from_vga();
					vga_write_sequencer(VGA_SC_MAP_MASK,	0xF);	/* map mask register = enable all planes */
					vga_write_GC(VGA_GC_ENABLE_SET_RESET,	0x00);	/* enable set/reset = no on all planes */
					vga_write_GC(VGA_GC_SET_RESET,		0x00);	/* set/reset register = all zero */
					vga_write_GC(VGA_GC_BIT_MASK,		0xFF);	/* all bits modified */

					vga_write_GC(VGA_GC_DATA_ROTATE,	0 | VGA_GC_DATA_ROTATE_OP_NONE); /* rotate=0 op=unmodified */
					vga_write_GC(VGA_GC_MODE,		0x02);	/* 256=0 CGAstyle=0 odd/even=0 readmode=0 writemode=2 (copy CPU bits to each plane) */
					{
						wr = vga_graphics_ram;
						for (y=0;y < h;y++) {
							color16 = (y>>3) * 0x101;
							/* NTS: performance hack: issue 16-bit memory I/O */
							for (x=0;x < w;x += 16) {
								*((VGA_ALPHA_PTR)wr) = (uint16_t)color16;
								wr += 2;
							}
						}
					}
					while (getch() != 13);

					vga_write_GC(VGA_GC_MODE,		0x02);	/* 256=0 CGAstyle=0 odd/even=0 readmode=0 writemode=2 */
					vga_write_GC(VGA_GC_DATA_ROTATE,	0 | VGA_GC_DATA_ROTATE_OP_NONE); /* rotate=0 op=none */
					{
						wr = vga_graphics_ram;
						for (y=0;y < h;y++) {
							vga_write_GC(VGA_GC_BIT_MASK,0x55 << (y & 1));
							color = y>>4;
							for (x=0;x < w;x += 8) {
								vga_force_read(wr); /* NTS: or else Watcom C++ optimizes the read out.
										            without the read the VGA latches will contain
											    other unexpected junk and we get the pattern wrong */
								vga_force_write(wr,color);
								wr++;
							}
						}
						while (getch() != 13);
					}

					vga_write_GC(VGA_GC_MODE,		0x02);	/* 256=0 CGAstyle=0 odd/even=0 readmode=0 writemode=2 */
					vga_write_GC(VGA_GC_DATA_ROTATE,	0 | VGA_GC_DATA_ROTATE_OP_AND); /* rotate=0 op=AND */
					{
						wr = vga_graphics_ram;
						for (y=0;y < h;y++) {
							vga_write_GC(VGA_GC_BIT_MASK,((y&6) == 0 ? 0xFF : 0xC0));
							color = 1 << ((y>>5)&3);
							for (x=0;x < w;x += 8) {
								vga_force_read(wr); /* NTS: or else Watcom C++ optimizes the read out.
										            without the read the VGA latches will contain
											    other unexpected junk and we get the pattern wrong */
								vga_force_write(wr,color);
								wr++;
							}
						}
						while (getch() != 13);
					}

					vga_write_GC(VGA_GC_MODE,		0x02);	/* 256=0 CGAstyle=0 odd/even=0 readmode=0 writemode=2 */
					vga_write_GC(VGA_GC_DATA_ROTATE,	0 | VGA_GC_DATA_ROTATE_OP_OR); /* rotate=0 op=OR */
					{
						wr = vga_graphics_ram;
						for (y=0;y < h;y++) {
							vga_write_GC(VGA_GC_BIT_MASK,((y&6) == 0 ? 0xFF : 0xC0));
							color = 8 << ((y>>5)&3);
							for (x=0;x < w;x += 8) {
								vga_force_read(wr); /* NTS: or else Watcom C++ optimizes the read out.
										            without the read the VGA latches will contain
											    other unexpected junk and we get the pattern wrong */
								vga_force_write(wr,color);
								wr++;
							}
						}
						while (getch() != 13);
					}

					vga_write_GC(VGA_GC_MODE,		0x02);	/* 256=0 CGAstyle=0 odd/even=0 readmode=0 writemode=2 */
					vga_write_GC(VGA_GC_DATA_ROTATE,	0 | VGA_GC_DATA_ROTATE_OP_XOR); /* rotate=0 op=XOR */
					{
						wr = vga_graphics_ram;
						for (y=0;y < h;y++) {
							vga_write_GC(VGA_GC_BIT_MASK,((y&6) == 0 ? 0xFF : 0xC0));
							for (x=0;x < w;x += 8) {
								vga_force_read(wr); /* NTS: or else Watcom C++ optimizes the read out.
										            without the read the VGA latches will contain
											    other unexpected junk and we get the pattern wrong */
								vga_force_write(wr,15);
								wr++;
							}
						}
						while (getch() != 13);
					}
				}

				/* now play with Attribute Controller registers (EGA palette mapping) */
				vga_write_AC(0,0);
				vga_write_AC(1,8);
				vga_write_AC(2,7);
				vga_write_AC(3,15);
				vga_AC_reenable_screen();
				while (getch() != 13);

				int10_setmode(3);
				update_state_from_vga();
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only EGA/VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'P') {
			if (vga_flags & VGA_IS_TANDY) {
				/* 160x200x16 */
				int10_setmode(8);
				{
					unsigned char row[160];
					unsigned int x,y;
					VGA_RAM_PTR wr;

					for (y=0;y < (160/2);y++)
						row[y] = ((y * 0x10) / 80) * 0x11;

					for (y=0;y < 80;y++) {
						wr = vga_graphics_ram + ((160/2) * (y>>1)) + ((y&1) << 13);
						for (x=0;x < (160/2);x++) wr[x] = row[x];
					}

					for (y=0;y < (160/2);y++) {
						x = (y * 0x20) / 80;
						row[y] = (x>>1) * 0x11;
						if (row[y] == 0xFF || (x&1) == 0) {
							row[y+(160/2)] = row[y];
						}
						else {
							row[y+(160/2)] = row[y] + 0x10;
							row[y] += 0x01;
						}
					}

					for (y=80;y < 160;) {
						wr = vga_graphics_ram + ((160/2) * (y>>1)) + ((y&1) << 13);
						for (x=0;x < (160/2);x++) wr[x] = row[x];
						y++;

						wr = vga_graphics_ram + ((160/2) * (y>>1)) + ((y&1) << 13);
						for (x=0;x < (160/2);x++) wr[x] = row[x+(160/2)];
						y++;
					}
				}
				while (getch() != 13);
				vga_tandy_setpalette(1,8);
				vga_tandy_setpalette(2,7);
				vga_tandy_setpalette(3,15);
				while (getch() != 13);

				/* 320x200x16 */
				int10_setmode(9);
				{
					unsigned char row[320];
					unsigned int x,y;
					VGA_RAM_PTR wr;

					for (y=0;y < (320/2);y++)
						row[y] = ((y * 0x10) / 160) * 0x11;

					for (y=0;y < 80;y++) {
						wr = vga_graphics_ram + ((320/2) * (y>>2)) + ((y&3) << 13);
						for (x=0;x < (320/2);x++) wr[x] = row[x];
					}

					for (y=0;y < (320/2);y++) {
						x = (y * 0x20) / 160;
						row[y] = (x>>1) * 0x11;
						if (row[y] == 0xFF || (x&1) == 0) {
							row[y+(320/2)] = row[y];
						}
						else {
							row[y+(320/2)] = row[y] + 0x10;
							row[y] += 0x01;
						}
					}

					for (y=80;y < 160;) {
						wr = vga_graphics_ram + ((320/2) * (y>>2)) + ((y&3) << 13);
						for (x=0;x < (320/2);x++) wr[x] = row[x];
						y++;

						wr = vga_graphics_ram + ((320/2) * (y>>2)) + ((y&3) << 13);
						for (x=0;x < (320/2);x++) wr[x] = row[x+(320/2)];
						y++;
					}
				}
				while (getch() != 13);
				vga_tandy_setpalette(1,8);
				vga_tandy_setpalette(2,7);
				vga_tandy_setpalette(3,15);
				while (getch() != 13);

				/* 640x200x4 */
				/* NTS: Oh, ick. I see... the 640x mode could be thought of as groups of pixels
				 *      per 16-bit WORD, where each byte is a monochromatic bitmap 8 pixels wide
				 *      and the two put together form 2-bit values!
				 *
				 *      byte 0 = 8x wide mono bitmap, forms lsb
				 *      byte 1 = 8x wide mono bitmap, forms msb */
				int10_setmode(10);
				{
					static uint16_t t4pat[4] = {
						0x0055,
						0x55AA,
						0xFF55,
						0xFFFF
					};
					uint16_t row[(640/8)*2];
					unsigned int x,y;
					VGA_ALPHA_PTR wr;

					for (y=0;y < (640/8);y++) {
						x = (y * 4) / (640/8);
						row[y] = (x & 1) * 0xFFU;
						row[y] |= ((x & 2) >> 1) * 0xFF00U;
					}

					for (y=0;y < 80;y++) {
						wr = vga_alpha_ram + ((640/8) * (y>>2)) + (((y&3) << 13)>>1);
						for (x=0;x < (640/8);x++) wr[x] = row[x];
					}

					for (y=0;y < (640/8);y++) {
						x = (y * 8) / (640/8);
						if (x & 1) {
							unsigned int c = t4pat[x>>1];
							row[y] = c;
							row[y+(640/8)] = 
								((((c&0xFF) << 1) | ((c&0xFF) >> 7)) & 0x00FF) |
								(((((c>>8)&0xFF) << 1) | (((c>>8)&0xFF) >> 7)) << 8);
						}
						else {
							row[y] = ((x & 2) >> 1) * 0xFFU;
							row[y] |= ((x & 4) >> 2) * 0xFF00U;
							row[y+(640/8)] = row[y];
						}
					}

					for (y=80;y < 160;) {
						wr = vga_alpha_ram + ((640/8) * (y>>2)) + (((y&3) << 13)>>1);
						for (x=0;x < (640/8);x++) wr[x] = row[x];
						y++;

						wr = vga_alpha_ram + ((640/8) * (y>>2)) + (((y&3) << 13)>>1);
						for (x=0;x < (640/8);x++) wr[x] = row[x+(640/8)];
						y++;
					}
				}
				while (getch() != 13);
				vga_tandy_setpalette(1,8);
				vga_tandy_setpalette(2,7);
				vga_tandy_setpalette(3,15);
				while (getch() != 13);

				/* TODO: figure out 640x200x16 */

				int10_setmode(3);
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only Tandy/CGA or compatible may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'D') {
			if (vga_flags & VGA_IS_CGA) { /* NTS: This test also works on EGA/VGA */
				int10_setmode(4);
				if ((vga_flags & (VGA_IS_EGA|VGA_IS_VGA|VGA_IS_TANDY)) == 0) /* this part of the test doesn't work on EGA/VGA nor does it work on Tandy/PCjr */
					vga_set_cga_palette_and_background(0,0);

				{
					unsigned int x,y;
					VGA_RAM_PTR wr;

					for (y=0;y < 80;y++) {
						wr = vga_graphics_ram + ((320/4) * (y>>1)) + ((y&1) << 13);
						for (x=0;x < (((320/4)*1)/4);x++) *wr++ = 0;
						for (   ;x < (((320/4)*2)/4);x++) *wr++ = 0x55;
						for (   ;x < (((320/4)*3)/4);x++) *wr++ = 0xAA;
						for (   ;x < (((320/4)*4)/4);x++) *wr++ = 0xFF;
					}
					for (;y < 160;y++) {
						wr = vga_graphics_ram + ((320/4) * (y>>1)) + ((y&1) << 13);
						for (x=0;x < (((320/4)*1)/8);x++) *wr++ = 0;
						for (   ;x < (((320/4)*2)/8);x++) *wr++ = 0x11 << ((y&1)<<1);
						for (   ;x < (((320/4)*3)/8);x++) *wr++ = 0x55;
						for (   ;x < (((320/4)*4)/8);x++) *wr++ = (y&1) ? 0x66 : 0x99;
						for (   ;x < (((320/4)*5)/8);x++) *wr++ = 0xAA;
						for (   ;x < (((320/4)*6)/8);x++) *wr++ = (y&1) ? 0xBB : 0xEE;
						for (   ;x < (((320/4)*8)/8);x++) *wr++ = 0xFF;
					}
				}
				if ((vga_flags & (VGA_IS_EGA|VGA_IS_VGA|VGA_IS_TANDY)) == 0) {/* this part of the test doesn't work on EGA/VGA */
					while (getch() != 13);
					vga_set_cga_palette_and_background(0,VGA_CGA_PALETTE_CS_BLUE);
					while (getch() != 13);
					vga_set_cga_palette_and_background(0,VGA_CGA_PALETTE_CS_ALT_INTENSITY);
					while (getch() != 13);
					vga_set_cga_palette_and_background(1,0);
					while (getch() != 13);
					vga_set_cga_palette_and_background(1,VGA_CGA_PALETTE_CS_ALT_INTENSITY);
					while (getch() != 13);
					vga_set_cga_mode(VGA_CGA_MODE_40WIDE|VGA_CGA_MODE_GRAPHICS|VGA_CGA_MODE_BW|VGA_CGA_MODE_VIDEO_ENABLE|VGA_CGA_MODE_VIDEO_640);
					vga_set_cga_palette_and_background(0,0xF); /* RGBI=1 aka white */
					while (getch() != 13);
					/* heheh... CGA composite mode. DOSBox actually emulates it too :) */
					/* see how it works? You're supposed to set BW=1 in 640x200 mode but we don't. on the composite video
					 * out this leaves the color subcarrier on where the fine pixels can get confused with the color subcarrier */
					vga_set_cga_mode(VGA_CGA_MODE_40WIDE|VGA_CGA_MODE_GRAPHICS|VGA_CGA_MODE_VIDEO_ENABLE|VGA_CGA_MODE_VIDEO_640);
					vga_set_cga_palette_and_background(0,0xF); /* RGBI=1 aka white */
					while (getch() != 13);
					{
						unsigned char c,color;
						unsigned int x,y;
						VGA_RAM_PTR wr;

						/* in composite mode make up something to show more colors */
						for (y=0;y < 200;y++) {
							color = (y*0x10)/200;
							c = color * 0x11;
							wr = vga_graphics_ram + ((320/4) * (y>>1)) + ((y&1) << 13);
							for (x=0;x < (320/4);x++) *wr++ = c;
						}
					}
					while (getch() != 13);
					/* heheh... CGA composite mode. DOSBox actually emulates it too :) */
					/* see how it works? You're supposed to set BW=1 in 640x200 mode but we don't. on the composite video
					 * out this leaves the color subcarrier on where the fine pixels can get confused with the color subcarrier */
					vga_set_cga_mode(VGA_CGA_MODE_40WIDE|VGA_CGA_MODE_GRAPHICS|VGA_CGA_MODE_BW|VGA_CGA_MODE_VIDEO_ENABLE|VGA_CGA_MODE_VIDEO_640);
					vga_set_cga_palette_and_background(0,0xF); /* RGBI=1 aka white */
					while (getch() != 13);
					vga_set_cga_mode(VGA_CGA_MODE_40WIDE|VGA_CGA_MODE_GRAPHICS|VGA_CGA_MODE_VIDEO_ENABLE);
					vga_set_cga_palette_and_background(1,VGA_CGA_PALETTE_CS_ALT_INTENSITY);
					while (getch() != 13);
				}
				else { /* EGA/VGA compatible test */
					/* NTS: We have to redraw it. Some VGA clones (primarily Intel 855/915/945 chipsets) do not properly
					 *      handle INT 10H mode changes with "preserve video memory" set. Not because they're assholes,
					 *      but because of the funky way that Intel chipsets interleave the data in actual RAM. */
					while (getch() != 13);
					int10_setmode(6);
					{
						unsigned int x,y;
						VGA_RAM_PTR wr;

						for (y=0;y < 80;y++) {
							wr = vga_graphics_ram + ((320/4) * (y>>1)) + ((y&1) << 13);
							for (x=0;x < (((320/4)*1)/4);x++) *wr++ = 0;
							for (   ;x < (((320/4)*2)/4);x++) *wr++ = 0x55;
							for (   ;x < (((320/4)*3)/4);x++) *wr++ = 0xAA;
							for (   ;x < (((320/4)*4)/4);x++) *wr++ = 0xFF;
						}
						for (;y < 160;y++) {
							wr = vga_graphics_ram + ((320/4) * (y>>1)) + ((y&1) << 13);
							for (x=0;x < (((320/4)*1)/8);x++) *wr++ = 0;
							for (   ;x < (((320/4)*2)/8);x++) *wr++ = 0x11 << ((y&1)<<1);
							for (   ;x < (((320/4)*3)/8);x++) *wr++ = 0x55;
							for (   ;x < (((320/4)*4)/8);x++) *wr++ = (y&1) ? 0x66 : 0x99;
							for (   ;x < (((320/4)*5)/8);x++) *wr++ = 0xAA;
							for (   ;x < (((320/4)*6)/8);x++) *wr++ = (y&1) ? 0xBB : 0xEE;
							for (   ;x < (((320/4)*8)/8);x++) *wr++ = 0xFF;
						}
					}
					while (getch() != 13);
				}

				int10_setmode(3);
				if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
			}
			else {
				vga_write("Only CGA or compatible may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == '!') {
			if (vga_flags & (VGA_IS_VGA|VGA_IS_EGA)) {
				unsigned char mono = (vga_ram_base == 0xB0000) ? 1 : 0;
				unsigned char c;

				vga_clear();
				vga_moveto(0,0);
				vga_write_color(7);
				vga_write("I will switch the VGA through all four map modes on each CRTC config.\n\n");
				vga_moveto(0,2);
				vga_write("            CUR  MONO COL\n");
				/* row = 2 */
				vga_write("A0000+128KB \n");
				vga_write("A0000+64KB  \n");
				vga_write("B0000+32KB  \n");
				vga_write("B8000+32KB  \n");
				vga_write("\n");
				vga_write_color(0x2F);

				for (c=0;c <= 3;c++) {
					vga_set_memory_map(c);
					vga_moveto(12,3+c);
					vga_write("OK");
				}

				vga_relocate_crtc(0);
				vga_write_CRTC(0xF,1);
				if (vga_read_CRTC(0xF) == 1) {
					for (c=0;c <= 3;c++) {
						vga_set_memory_map(c);
						vga_moveto(12+5,3+c);
						vga_write("OK");
					}
				}

				vga_relocate_crtc(1);
				vga_write_CRTC(0xF,1);
				if (vga_read_CRTC(0xF) == 1) {
					for (c=0;c <= 3;c++) {
						vga_set_memory_map(c);
						vga_moveto(12+(5*2),3+c);
						vga_write("OK");
					}
				}

				vga_set_memory_map(mono ? 2 : 3);
				vga_relocate_crtc(mono ? 0 : 1);
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
			else {
				vga_write("Only EGA/VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'H') {
			if (vga_flags & VGA_IS_HGC) {
				vga_turn_on_hgc();
				{
					unsigned char pat;
					unsigned int x,y;
					VGA_RAM_PTR wr;

					/* clear */
					wr = vga_graphics_ram;
					for (x=0;x < 0x8000;x++) *wr++ = 0x00;

					/* draw hatch pattern */
					for (y=0;y < 348;y++) {
						wr = vga_graphics_ram + ((y>>2) * (720/8)) + ((y&3) << 13);
						pat = 0x55 << (y & 1);
						for (x=0;x < (720/8);x++) *wr++ = pat;
					}

					/* and another pattern */
					for (y=0;y <= 120;y++) {
						wr = vga_graphics_ram + ((y>>2) * (720/8)) + ((y&3) << 13) + 14;
						pat = (y % 4) == 0 ? 0xFF : 0x88;
						for (x=0;x < (240/8);x++) *wr++ = pat;
					}
				}
				while (getch() != 13);

				vga_turn_off_hgc();
			}
			else {
				vga_write("Only HGC hercules MDA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'T') {
			int10_setmode(3);
			update_state_from_vga();
			if (vga_flags & VGA_IS_VGA && vga_want_9wide != 0xFF) vga_set_9wide(vga_want_9wide);
		}
		else if (c == 'M') {
			if (vga_flags & (VGA_IS_VGA|VGA_IS_EGA)) {
				vga_switch_to_mono();
				update_state_from_vga();
			}
			else {
				vga_write("Only EGA/VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
		else if (c == 'C') {
			if (vga_flags & (VGA_IS_VGA|VGA_IS_EGA)) {
				vga_switch_to_color();
				update_state_from_vga();
			}
			else {
				vga_write("Only EGA/VGA may do that\n");
				vga_write_sync();
				vga_sync_bios_cursor();
				while (getch() != 13);
			}
		}
	}
	vga_sync_bios_cursor();

#if defined(TARGET_WINDOWS)
	DisplayDibDoEnd();
	if (DisplayDibUnloadDLL())
		MessageBox(hwnd,dispDibLastError,"Failed to unload DISPDIB.DLL",MB_OK);

# if TARGET_MSDOS == 32
	FreeWin16EB();
# endif
#endif

	return 0;
}
