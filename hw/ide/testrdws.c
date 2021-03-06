
#include <stdio.h>
#include <conio.h> /* this is where Open Watcom hides the outp() etc. functions */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <ctype.h>
#include <fcntl.h>
#include <dos.h>

#include <hw/vga/vga.h>
#include <hw/pci/pci.h>
#include <hw/dos/dos.h>
#include <hw/8254/8254.h>		/* 8254 timer */
#include <hw/8259/8259.h>		/* 8259 PIC interrupts */
#include <hw/vga/vgagui.h>
#include <hw/vga/vgatty.h>
#include <hw/ide/idelib.h>

#include "testutil.h"
#include "testmbox.h"
#include "testcmui.h"
#include "testbusy.h"
#include "testpiot.h"
#include "testrvfy.h"
#include "testrdwr.h"
#include "testidnt.h"
#include "testcdej.h"
#include "testpiom.h"
#include "testtadj.h"
#include "testcdrm.h"
#include "testmumo.h"
#include "testrdts.h"
#include "testrdws.h"
#include "test.h"

#include "testnop.h"
#include "testpwr.h"

static unsigned char drive_write_warning = 1;

static const char *drive_write_test_menustrings[] = {
	"Show IDE register taskfile",		/* 0 */
	"*mode*",				/* 1 */ /* rewritten (CHS, LBA, CHS MULTI, etc) */
	drive_readwrite_test_geo,
	drive_readwrite_test_chs,
	drive_readwrite_test_numsec,
	drive_readwrite_test_mult,
	"Write sectors",
	"Write sectors continuously"
};

static void do_hdd_drive_write_test(struct ide_controller *ide,unsigned char which,unsigned char continuous,struct drive_rw_test_info *nfo) {
	unsigned char user_esc = 0;
	struct vga_msg_box vgabox;
	struct ide_taskfile *tsk;
	unsigned long tlen_sect;
	unsigned int cleared=0;
	unsigned long tlen;
	unsigned int i;
	int c;

	if (nfo->read_sectors == 0) return;

	/* multiple mode: make sure the multiple sector count doesn't exceed our buffer size.
	 * if it does, try to set multiple count to lesser value. */
	if (nfo->mode == DRIVE_RW_MODE_CHSMULTIPLE || nfo->mode == DRIVE_RW_MODE_LBAMULTIPLE || nfo->mode == DRIVE_RW_MODE_LBA48_MULTIPLE) {
		if (nfo->multiple_sectors > ((unsigned long)sizeof(cdrom_sector) / 512UL) && nfo->multiple_sectors > 1) {
			/* even though most IDE devices really only support power-of-2 sizes, we do a scan downward anyway */
			c = ((unsigned long)sizeof(cdrom_sector) / 512UL);
			do {
				if (c <= 1) break;
				do_ide_set_multiple_mode(ide,which,c);
				{
					uint16_t info[256];

					do_ide_identify((unsigned char*)info,sizeof(info),ide,which,0xEC/*ATA IDENTIFY DEVICE*/);
					drive_rw_test_nfo.multiple_sectors = info[59]&0xFF;
				}

				if (nfo->multiple_sectors > ((unsigned long)sizeof(cdrom_sector) / 512UL))
					c--;
				else
					break;
			} while (1);
		}

		if (nfo->multiple_sectors > ((unsigned long)sizeof(cdrom_sector) / 512UL))
			return;
		if (nfo->multiple_sectors == 0)
			return;
	}

	idelib_controller_reset_irq_counter(ide); /* IRQ will fire after command completion */
	tsk = idelib_controller_get_taskfile(ide,-1/*selected drive*/);

again:	/* jump point: send execution back here for another sector */
	if (do_ide_controller_user_wait_busy_controller(ide) != 0 || do_ide_controller_user_wait_drive_ready(ide) < 0)
		return;
	idelib_controller_ack_irq(ide); /* <- make sure to ack IRQ */

	tlen_sect = nfo->read_sectors;
	if (tlen_sect > ((unsigned long)sizeof(cdrom_sector) / 512UL))
		tlen_sect = ((unsigned long)sizeof(cdrom_sector) / 512UL);

	/* C/H/S continuous: limit reads to within track, don't cross. we can't assume the drive will do that. */
	if (continuous && (nfo->mode == DRIVE_RW_MODE_CHS || nfo->mode == DRIVE_RW_MODE_CHSMULTIPLE)) {
		if ((nfo->sector + tlen_sect) > nfo->num_sector)
			tlen_sect = (nfo->num_sector + 1 - nfo->sector);
	}

	tlen = tlen_sect * 512UL;

	if (nfo->mode == DRIVE_RW_MODE_LBA48 || nfo->mode == DRIVE_RW_MODE_LBA48_MULTIPLE) {
		tsk->sector_count = tlen_sect;
		tsk->lba0_3 = nfo->lba & 0xFF;
		tsk->lba1_4 = (nfo->lba >> 8) & 0xFF;
		tsk->lba2_5 = (nfo->lba >> 16) & 0xFF;
		tsk->lba0_3 |= ((nfo->lba >> 24) & 0xFF) << 8;
		tsk->lba1_4 |= ((nfo->lba >> 32) & 0xFF) << 8;
		tsk->lba2_5 |= ((nfo->lba >> 40) & 0xFF) << 8;
		tsk->head_select = (which << 4) | 0x40;

		if (nfo->mode == DRIVE_RW_MODE_LBA48)
			tsk->command = 0x34; /* WRITE SECTORS EXT */
		else if (nfo->mode == DRIVE_RW_MODE_LBA48_MULTIPLE)
			tsk->command = 0x39; /* WRITE MULTIPLE EXT */

		if (idelib_controller_apply_taskfile(ide,0xFC/*base_io+2-7*/,IDELIB_TASKFILE_LBA48_UPDATE|IDELIB_TASKFILE_LBA48/*set LBA48*/) < 0)
			return;
	}
	else {
		if (tsk->sector_count > 256) return;
		tsk->sector_count = (tlen_sect&0xFF);
		if (nfo->mode == DRIVE_RW_MODE_CHS || nfo->mode == DRIVE_RW_MODE_CHSMULTIPLE) {
			tsk->chs_sector = nfo->sector;
			tsk->chs_cylinder_low = nfo->cylinder & 0xFF;
			tsk->chs_cylinder_high = (nfo->cylinder >> 8) & 0xFF;
			tsk->head_select = (nfo->head & 0xF) | (which << 4) | 0xA0;
		}
		else if (nfo->mode == DRIVE_RW_MODE_LBA || nfo->mode == DRIVE_RW_MODE_LBAMULTIPLE) {
			tsk->lba0_3 = nfo->lba & 0xFF;
			tsk->lba1_4 = (nfo->lba >> 8) & 0xFF;
			tsk->lba2_5 = (nfo->lba >> 16) & 0xFF;
			tsk->head_select = ((nfo->lba >> 24) & 0xF) | (which << 4) | 0xE0;
		}

		if (nfo->mode == DRIVE_RW_MODE_CHS || nfo->mode == DRIVE_RW_MODE_LBA)
			tsk->command = 0x30; /* WRITE SECTORS */
		else if (nfo->mode == DRIVE_RW_MODE_CHSMULTIPLE || nfo->mode == DRIVE_RW_MODE_LBAMULTIPLE)
			tsk->command = 0xC5; /* WRITE MULTIPLE */

		if (idelib_controller_apply_taskfile(ide,0xFC/*base_io+2-7*/,IDELIB_TASKFILE_LBA48_UPDATE/*clear LBA48*/) < 0)
			return;
	}

	/* wait for drive ready. drive will NOT fire an IRQ until the write is done. */
	if (do_ide_controller_user_wait_busy_controller(ide) != 0 || do_ide_controller_user_wait_drive_ready(ide) < 0)
		return;

	if (!idelib_controller_is_error(ide)) { /* OK. success. now read the data */
		unsigned int ret_len = 0,drq_len;

		/* make test data */
		for (i=0;i < tlen;i++) cdrom_sector[i] = i + (i >> 8);

		while (ret_len < tlen) {
			if (idelib_controller_is_error(ide)) {
				vga_msg_box_create(&vgabox,"Error",0,0);
				wait_for_enter_or_escape();
				vga_msg_box_destroy(&vgabox);
				break;
			}

			if (do_ide_controller_user_wait_drive_drq(ide) < 0) {
				user_esc = 1;
				break;
			}

			if (nfo->mode == DRIVE_RW_MODE_CHSMULTIPLE || nfo->mode == DRIVE_RW_MODE_LBAMULTIPLE ||
				nfo->mode == DRIVE_RW_MODE_LBA48_MULTIPLE) {
				drq_len = nfo->multiple_sectors * 512;
				if ((ret_len+drq_len) > tlen) drq_len = tlen - ret_len;
			}
			else
				drq_len = 512;

			/* OK. write it out and acknowledge */
			idelib_write_pio_general(cdrom_sector+ret_len,drq_len,ide,IDELIB_PIO_WIDTH_DEFAULT);
			ret_len += drq_len;

			/* you're supposed to wait for IRQ after writing the sector */
			if (ide->flags.io_irq_enable) {
				if (do_ide_controller_user_wait_irq(ide,1) < 0) {
					user_esc = 1;
					break;
				}
				idelib_controller_reset_irq_counter(ide); /* IRQ will fire after command completion */
				idelib_controller_ack_irq(ide); /* <- or else it won't fire again */
			}

			if (do_ide_controller_user_wait_busy_controller(ide) != 0 || do_ide_controller_user_wait_drive_ready(ide) < 0) {
				user_esc = 1;
				break;
			}
		}

		/* ---- draw contents on the screen ---- */
		vga_write_color(0x0E);
		if (!cleared) {
			vga_clear();
			cleared = 1;
		}

		vga_moveto(0,0);
		vga_write("Sector writing:\n");
		if (nfo->mode == DRIVE_RW_MODE_CHS || nfo->mode == DRIVE_RW_MODE_CHSMULTIPLE) {
			sprintf(tmp,"CHS %u/%u/%u    ",nfo->cylinder,nfo->head,nfo->sector); vga_write(tmp);
		}
		else {
			sprintf(tmp,"%llu-%llu",nfo->lba,nfo->lba+(unsigned long long)tlen_sect-1ULL); vga_write(tmp);
		}

		if (!idelib_controller_is_error(ide)) { /* OK. success. now read the data */
			vga_write_color(0x0A);
			vga_write(" PASSED\n");
		}
		else {
			vga_write_color(0x0C);
			vga_write(" FAILED\n");
		}

		if (continuous && !user_esc) {
			if (kbhit()) {
				c = getch();
				if (c == 0) c = getch() << 8;
			}
			else {
				c = 0;
			}
		}
		else {
			if ((c=wait_for_enter_or_escape()) == 27)
				return; /* allow user to exit early by hitting ESC */
		}

		if (c != 27 && !user_esc) {
			/* if the user hit ENTER, then read another sector and display that too */
			if (nfo->mode == DRIVE_RW_MODE_CHS || nfo->mode == DRIVE_RW_MODE_CHSMULTIPLE) {
				nfo->sector += (unsigned long long)tlen_sect;
				while (nfo->sector > nfo->num_sector) {
					nfo->sector -= nfo->num_sector;
					nfo->head++;
				}
				while (nfo->head >= nfo->num_head) {
					nfo->head -= nfo->num_head;
					nfo->cylinder++;
				}

				if (nfo->cylinder >= 16384) {
					nfo->cylinder = 16383;
					nfo->sector = nfo->num_sector;
					nfo->head = nfo->num_head - 1;
				}

				nfo->lba  = (unsigned long long)nfo->cylinder * (unsigned long long)nfo->num_sector * (unsigned long long)nfo->num_head;
				nfo->lba += (unsigned long long)nfo->head * (unsigned long long)nfo->num_sector;
				nfo->lba += (unsigned long long)nfo->sector - 1ULL;
			}
			else {
				nfo->lba += (unsigned long long)tlen_sect;

				nfo->sector = (int)(nfo->lba % (unsigned long long)nfo->num_sector) + 1;
				nfo->head = (int)((nfo->lba / (unsigned long long)nfo->num_sector) % (unsigned long long)nfo->num_head);
				if (nfo->lba >= (16384ULL * (unsigned long long)nfo->num_sector * (unsigned long long)nfo->num_head)) {
					nfo->cylinder = 16383;
					nfo->sector = nfo->num_sector;
					nfo->head = nfo->num_head - 1;
				}
				else {
					nfo->cylinder = (int)((nfo->lba / (unsigned long long)nfo->num_sector) / (unsigned long long)nfo->num_head);
				}
			}

			goto again;
		}
	}
	else {
		common_ide_success_or_error_vga_msg_box(ide,&vgabox);
		wait_for_enter_or_escape();
		vga_msg_box_destroy(&vgabox);
	}
}

void do_drive_write_test(struct ide_controller *ide,unsigned char which) {
	struct menuboxbounds mbox;
	char backredraw=1;
	VGA_ALPHA_PTR vga;
	unsigned int x,y;
	int select=-1;
	char redraw=1;
	int c;

	if (drive_write_warning) {
		struct vga_msg_box box;

		vga_msg_box_create(&box,
			"WARNING: This test is destructive. It overwrites the data on the disk.\n"
			"         If you value the data on your hard drive DO NOT RUN THIS TEST.\n"
			"\n"
			"Hit ESC to cancel, ENTER to continue"
			,0,0);
		c = wait_for_enter_or_escape();
		vga_msg_box_destroy(&box);
		if (c == 27) return;

		vga_msg_box_create(&box,
			"WARNING: Are you sure? The contents of the hard disk will be overwritten\n"
			"         in the process of carrying out this test!\n"
			"\n"
			"Hit ESC to cancel, ENTER to continue"
			,0,0);
		c = wait_for_enter_or_escape();
		vga_msg_box_destroy(&box);
		if (c == 27) return;

		vga_msg_box_create(&box,
			"FINAL WARNING: This test will overwrite the sectors on the disk with\n"
			"               test data. The test is not undoable. ARE YOU SURE?\n"
			"\n"
			"If you confirm, this warning will not show again until the test program\n"
			"is restarted.\n"
			"\n"
			"Hit ESC to cancel, ENTER to continue"
			,0,0);
		c = wait_for_enter_or_escape();
		vga_msg_box_destroy(&box);
		if (c == 27) return;

		/* if you say so! */
		drive_write_warning = 0;
	}

	/* UI element vars */
	menuboxbounds_set_def_list(&mbox,/*ofsx=*/4,/*ofsy=*/7,/*cols=*/1);
	menuboxbounds_set_item_strings_arraylen(&mbox,drive_write_test_menustrings);

	while (1) {
		if (backredraw) {
			vga = vga_alpha_ram;
			backredraw = 0;
			redraw = 1;

			for (y=0;y < vga_height;y++) {
				for (x=0;x < vga_width;x++) {
					*vga++ = 0x1E00 + 177;
				}
			}

			vga_moveto(0,0);

			vga_write_color(0x1F);
			vga_write("        IDE r/w tests ");
			sprintf(tmp,"@%X",ide->base_io);
			vga_write(tmp);
			if (ide->alt_io != 0) {
				sprintf(tmp," alt %X",ide->alt_io);
				vga_write(tmp);
			}
			if (ide->irq >= 0) {
				sprintf(tmp," IRQ %d",ide->irq);
				vga_write(tmp);
			}
			vga_write(which ? " Slave" : " Master");

			sprintf(tmp," lba=%u lba48=%u multiple=%u",
				drive_rw_test_nfo.can_do_lba?1:0,
				drive_rw_test_nfo.can_do_lba48?1:0,
				drive_rw_test_nfo.can_do_multiple?1:0);
			vga_write(tmp);

			while (vga_pos_x < vga_width && vga_pos_x != 0) vga_writec(' ');

			vga_write_color(0xC);
			vga_write("WARNING: This code talks directly to your hard disk controller.");
			while (vga_pos_x < vga_width && vga_pos_x != 0) vga_writec(' ');
			vga_write_color(0xC);
			vga_write("         If you value the data on your hard drive do not run this program.");
			while (vga_pos_x < vga_width && vga_pos_x != 0) vga_writec(' ');
		}

		if (redraw) {
			redraw = 0;

			drive_write_test_menustrings[1] = drive_readwrite_test_modes[drive_rw_test_nfo.mode];

			sprintf(drive_readwrite_test_geo,"Geometry: C/H/S %u/%u/%u LBA %llu",
				drive_rw_test_nfo.num_cylinder,
				drive_rw_test_nfo.num_head,
				drive_rw_test_nfo.num_sector,
				drive_rw_test_nfo.max_lba);

			sprintf(drive_readwrite_test_chs,"Position: C/H/S %u/%u/%u LBA %llu",
				drive_rw_test_nfo.cylinder,
				drive_rw_test_nfo.head,
				drive_rw_test_nfo.sector,
				drive_rw_test_nfo.lba);

			sprintf(drive_readwrite_test_numsec,"Number of sectors per read: %u",
				drive_rw_test_nfo.read_sectors);

			sprintf(drive_readwrite_test_mult,"Multiple mode: %u sectors (max=%u)",
				drive_rw_test_nfo.multiple_sectors,
				drive_rw_test_nfo.max_multiple_sectors);

			vga_moveto(mbox.ofsx,mbox.ofsy - 2);
			vga_write_color((select == -1) ? 0x70 : 0x0F);
			vga_write("Back to IDE controller main menu");
			while (vga_pos_x < (mbox.width+mbox.ofsx) && vga_pos_x != 0) vga_writec(' ');

			menuboxbound_redraw(&mbox,select);
		}

		c = getch();
		if (c == 0) c = getch() << 8;

		if (c == 27) {
			break;
		}
		else if (c == 13) {
			if (select == -1)
				break;

			switch (select) {
				case 0: /* show IDE register taskfile */
					do_common_show_ide_taskfile(ide,which);
					redraw = backredraw = 1;
					break;
				case 1: /* Mode */
					do_drive_readwrite_test_choose_mode(ide,which,&drive_rw_test_nfo);
					redraw = backredraw = 1;
					break;
				case 2: /* Edit geometry/max LBA */
					do_drive_readwrite_edit_chslba(ide,which,&drive_rw_test_nfo,/*editgeo*/1);
					redraw = 1;
					break;
				case 3: /* Edit position */
					do_drive_readwrite_edit_chslba(ide,which,&drive_rw_test_nfo,/*editgeo*/0);
					redraw = 1;
					break;
				case 4: /* Number of sectors */
					c = prompt_sector_count();
					if (c >= 1 && c <= 256) {
						drive_rw_test_nfo.read_sectors = c;
						redraw = 1;
					}
					break;
				case 5: /* Multiple mode sector count */
					c = prompt_sector_count();
					if (c >= 0 && c <= 255) {
						do_ide_set_multiple_mode(ide,which,c);
						{
							uint16_t info[256];

							do_ide_identify((unsigned char*)info,sizeof(info),ide,which,0xEC/*ATA IDENTIFY DEVICE*/);
							drive_rw_test_nfo.multiple_sectors = info[59]&0xFF;
						}
						redraw = 1;
					}
					break;
				case 6: /*Read sectors*/
				case 7: /*Read sectors continuously*/
					do_hdd_drive_write_test(ide,which,/*continuous=*/(select==7),&drive_rw_test_nfo);
					redraw = backredraw = 1;
					break;
			};
		}
		else if (c == 0x4800) {
			if (--select < -1)
				select = mbox.item_max;

			redraw = 1;
		}
		else if (c == 0x4B00) { /* left */
			switch (select) {
				case 1: /* Mode */
					do {
						if (drive_rw_test_nfo.mode == 0)
							drive_rw_test_nfo.mode = DRIVE_RW_MODE_MAX-1;
						else
							drive_rw_test_nfo.mode--;
					} while (!drive_rw_test_mode_supported(&drive_rw_test_nfo));
					break;
				case 4: /* Number of sectors */
					if (drive_rw_test_nfo.read_sectors > 1)
						drive_rw_test_nfo.read_sectors--;
					break;
			};

			redraw = 1;
		}
		else if (c == 0x4D00) { /* right */
			switch (select) {
				case 1: /* Mode */
					do {
						if (++drive_rw_test_nfo.mode >= DRIVE_RW_MODE_MAX)
							drive_rw_test_nfo.mode = 0;
					} while (!drive_rw_test_mode_supported(&drive_rw_test_nfo));
					break;
				case 4: /* Number of sectors */
					if (drive_rw_test_nfo.read_sectors < 256)
						drive_rw_test_nfo.read_sectors++;
					break;
			};

			redraw = 1;
		}
		else if (c == 0x5000) {
			if (++select > mbox.item_max)
				select = -1;

			redraw = 1;
		}
	}
}

