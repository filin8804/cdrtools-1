/* @(#)eltorito.c	1.15 00/04/08 joerg */
#ifndef lint
static	char sccsid[] =
	"@(#)eltorito.c	1.15 00/04/08 joerg";

#endif
/*
 * Program eltorito.c - Handle El Torito specific extensions to iso9660.
 *

   Written by Michael Fulbright <msf@redhat.com> (1996).

   Copyright 1996 RedHat Software, Incorporated

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   */


#include "config.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unixstd.h>
#include <stdxlib.h>
#include <fctldefs.h>

#include "mkisofs.h"
#include "match.h"
#include "iso9660.h"
#include "diskmbr.h"
#include "bootinfo.h"

#ifdef	USE_LIBSCHILY
#include <standard.h>
#endif
#include <utypes.h>
#include <intcvt.h>

#undef MIN
#define MIN(a, b) (((a) < (b))? (a): (b))

static struct eltorito_validation_entry valid_desc;
static struct eltorito_defaultboot_entry default_desc;
static struct eltorito_boot_descriptor gboot_desc;
static struct disk_master_boot_record disk_mbr;
static unsigned int bcat_de_flags;

	void	init_boot_catalog	__PR((const char *path));
	void	insert_boot_cat		__PR((void));
	void	get_torito_desc		__PR((struct eltorito_boot_descriptor *boot_desc));
static	int	tvd_write		__PR((FILE * outfile));


/*
 * Make sure any existing boot catalog is excluded
 */
void
init_boot_catalog(path)
	const char	*path;
{
	char           *bootpath;	/* filename of boot catalog */

	bootpath = (char *) e_malloc(strlen(boot_catalog) + strlen(path) + 2);
	strcpy(bootpath, path);
	if (bootpath[strlen(bootpath) - 1] != '/') {
		strcat(bootpath, "/");
	}
	strcat(bootpath, boot_catalog);

	/*
	 * we are going to create a virtual catalog file
	 * - so make sure any existing is excluded
	 */
	add_match(bootpath);

	/* flag the file as a memory file */
	bcat_de_flags = MEMORY_FILE;

	/* find out if we want to "hide" this file */
	if (i_matches(boot_catalog) || i_matches(bootpath))
		bcat_de_flags |= INHIBIT_ISO9660_ENTRY;

	if (j_matches(boot_catalog) || j_matches(bootpath))
		bcat_de_flags |= INHIBIT_JOLIET_ENTRY;

	free(bootpath);

}/* init_boot_catalog(... */

/*
 * Create a boot catalog file in memory - mkisofs already uses this type of
 * file for the TRANS.TBL files. Therefore the boot catalog is set up in
 * similar way
 */
void
insert_boot_cat()
{
	struct directory_entry *de,
	               *s_entry;
	char           *p1,
	               *p2,
	               *p3;
	struct directory *this_dir,
	               *dir;
	char           *buffer;

	init_fstatbuf();

	buffer = (char *) e_malloc(SECTOR_SIZE);
	memset(buffer, 0, SECTOR_SIZE);

	/*
	 * try to find the directory that will contain the boot.cat file
	 * - not very neat, but I can't think of a better way
	 */
	p1 = strdup(boot_catalog);

	/* get dirname (p1) and basename (p2) of boot.cat */
	if ((p2 = strrchr(p1, '/')) != NULL) {
		*p2 = '\0';
		p2++;

		/* find the dirname directory entry */
		de = search_tree_file(root, p1);
		if (!de) {
#ifdef	USE_LIBSCHILY
			comerrno(EX_BAD,
			"Uh oh, I cant find the boot catalog directory '%s'!\n",
								p1);
#else
			fprintf(stderr,
			"Uh oh, I cant find the boot catalog directory '%s'!\n",
								p1);
			exit(1);
#endif
		}
		this_dir = 0;

		/* get the basename (p3) of the directory */
		if ((p3 = strrchr(p1, '/')) != NULL)
			p3++;
		else
			p3 = p1;

		/* find the correct sub-directory entry */
		for (dir = de->filedir->subdir; dir; dir = dir->next)
			if (!(strcmp(dir->de_name, p3)))
				this_dir = dir;

		if (this_dir == 0) {
#ifdef	USE_LIBSCHILY
			comerrno(EX_BAD,
			"Uh oh, I cant find the boot catalog directory '%s'!\n",
								p3);
#else
			fprintf(stderr,
			"Uh oh, I cant find the boot catalog directory '%s'!\n",
								p3);
			exit(1);
#endif
		}
	} else {
		/* boot.cat is in the root directory */
		this_dir = root;
		p2 = p1;
	}

	/*
	 * make a directory entry in memory (using the same set up as for table
	 * entries
	 */
	s_entry = (struct directory_entry *)
		e_malloc(sizeof(struct directory_entry));
	memset(s_entry, 0, sizeof(struct directory_entry));
	s_entry->next = this_dir->contents;
	this_dir->contents = s_entry;

	s_entry->isorec.flags[0] = ISO_FILE;
	s_entry->priority = 32768;
	iso9660_date(s_entry->isorec.date, fstatbuf.st_mtime);
	s_entry->inode = TABLE_INODE;
	s_entry->dev = (dev_t) UNCACHED_DEVICE;
	set_723(s_entry->isorec.volume_sequence_number,
						volume_sequence_number);
	set_733((char *) s_entry->isorec.size, SECTOR_SIZE);
	s_entry->size = SECTOR_SIZE;
	s_entry->filedir = this_dir;
	s_entry->name = strdup(p2);
	iso9660_file_length(p2, s_entry, 0);

	/* flag file as necessary */
	s_entry->de_flags = bcat_de_flags;

	if (use_RockRidge && !(bcat_de_flags & INHIBIT_ISO9660_ENTRY)) {
		fstatbuf.st_mode = 0444 | S_IFREG;
		fstatbuf.st_nlink = 1;
		generate_rock_ridge_attributes("",
			p2, s_entry,
			&fstatbuf, &fstatbuf, 0);
	}
	/*
	 *  memory files are stored at s_entry->table
	 * - but this is also used for each s_entry to generate
	 * TRANS.TBL entries. So if we are generating tables,
	 * store the TRANS.TBL data here for the moment
	 */
	if (generate_tables && !(bcat_de_flags & INHIBIT_ISO9660_ENTRY)) {
		sprintf(buffer, "F\t%s\n", s_entry->name);

		/* copy the TRANS.TBL entry info and clear the buffer */
		s_entry->table = strdup(buffer);
		memset(buffer, 0, SECTOR_SIZE);

		/*
		 * store the (empty) file data in the
		 * unused s_entry->whole_name element for the time being
	 	 * - this will be transferred to s_entry->table after any
		 * TRANS.TBL processing later
		 */
		s_entry->whole_name = buffer;
	} else {
		/* store the (empty) file data in the s_entry->table element */
		s_entry->table = buffer;
		s_entry->whole_name = NULL;
	}
}

void
get_torito_desc(boot_desc)
	struct eltorito_boot_descriptor	*boot_desc;
{
	int             bootmbr;
	int             checksum;
	unsigned char  *checksum_ptr;
	struct directory_entry *de;	/* Boot file */
	struct directory_entry *de2;	/* Boot catalog */
	int             i;
	int             nsectors;
	int             geosec;

	memset(boot_desc, 0, sizeof(*boot_desc));
	boot_desc->type[0] = 0;
	memcpy(boot_desc->id, ISO_STANDARD_ID, sizeof(ISO_STANDARD_ID));
	boot_desc->version[0] = 1;

	memcpy(boot_desc->system_id, EL_TORITO_ID, sizeof(EL_TORITO_ID));

	/*
	 * search from root of iso fs to find boot catalog
	 * - we already know where the boot catalog is
	 * - we created it above - but lets search for it anyway
    	 * - good sanity check!
	 */
	de2 = search_tree_file(root, boot_catalog);
	if (!de2 || !(de2->de_flags & MEMORY_FILE)) {
#ifdef	USE_LIBSCHILY
		comerrno(EX_BAD, "Uh oh, I cant find the boot catalog '%s'!\n",
							boot_catalog);
#else
		fprintf(stderr, "Uh oh, I cant find the boot catalog '%s'!\n",
							boot_catalog);
		exit(1);
#endif
	}
	set_731(boot_desc->bootcat_ptr,
		(unsigned int) get_733(de2->isorec.extent));

	/* now adjust boot catalog lets find boot image first */
	de = search_tree_file(root, boot_image);
	if (!de) {
#ifdef	USE_LIBSCHILY
		comerrno(EX_BAD, "Uh oh, I cant find the boot image '%s' !\n",
							boot_image);
#else
		fprintf(stderr, "Uh oh, I cant find the boot image '%s' !\n",
							boot_image);
		exit(1);
#endif
	}
	/*
	 * we have the boot image, so write boot catalog information
	 * Next we write out the primary descriptor for the disc
	 */
	memset(&valid_desc, 0, sizeof(valid_desc));
	valid_desc.headerid[0] = 1;
	valid_desc.arch[0] = EL_TORITO_ARCH_x86;

	/*
	 * we'll shove start of publisher id into id field,
	 * may get truncated but who really reads this stuff!
	 */
	if (publisher)
		memcpy_max(valid_desc.id, publisher,
						MIN(23, strlen(publisher)));

	valid_desc.key1[0] = (char) 0x55;
	valid_desc.key2[0] = (char) 0xAA;

	/* compute the checksum */
	checksum = 0;
	checksum_ptr = (unsigned char *) &valid_desc;
	/* Set checksum to 0 before computing checksum */
	set_721(valid_desc.cksum, 0);
	for (i = 0; i < sizeof(valid_desc); i += 2) {
		checksum += (unsigned int) checksum_ptr[i];
		checksum += ((unsigned int) checksum_ptr[i + 1]) * 256;
	}

	/* now find out the real checksum */
	checksum = -checksum;
	set_721(valid_desc.cksum, (unsigned int) checksum);

	/* now make the initial/default entry for boot catalog */
	memset(&default_desc, 0, sizeof(default_desc));
	default_desc.boot_id[0] = (char) not_bootable ?
				EL_TORITO_NOT_BOOTABLE : EL_TORITO_BOOTABLE;

	/* use default BIOS loadpnt */
	set_721(default_desc.loadseg, load_addr);

	/*
	 * figure out size of boot image in 512-byte sectors.
	 * However, round up to the nearest integral CD (2048-byte) sector.
	 * This is only used for no-emulation booting.
	 */
	nsectors = load_size ? load_size : ((de->size + 2047) / 2048) * 4;
	fprintf(stderr, "\nSize of boot image is %d sectors -> ", nsectors);

	if (hard_disk_boot) {
		/* sanity test hard disk boot image */
		default_desc.boot_media[0] = EL_TORITO_MEDIA_HD;
		fprintf(stderr, "Emulating a hard disk\n");

		/* read MBR */
		bootmbr = open(de->whole_name, O_RDONLY | O_BINARY);
		if (bootmbr == -1) {
#ifdef	USE_LIBSCHILY
			comerr("Error opening boot image '%s' for read.\n",
							de->whole_name);
#else
			fprintf(stderr,
				"Error opening boot image '%s' for read.\n",
							de->whole_name);
			perror("");
			exit(1);
#endif
		}
		if (read(bootmbr, &disk_mbr, sizeof(disk_mbr)) !=
							sizeof(disk_mbr)) {
#ifdef	USE_LIBSCHILY
			comerr("Error reading MBR from boot image '%s'.\n",
							de->whole_name);
#else
			fprintf(stderr,
				"Error reading MBR from boot image '%s'.\n",
							de->whole_name);
			exit(1);
#endif
		}
		close(bootmbr);
		if (la_to_u_2_byte(disk_mbr.magic) != MBR_MAGIC) {
#ifdef	USE_LIBSCHILY
			errmsgno(EX_BAD,
			"Warning: boot image '%s' MBR is not a boot sector.\n",
							de->whole_name);
#else
			fprintf(stderr,
				"Warning: boot image '%s' MBR is not a boot sector.\n",
							de->whole_name);
#endif
		}
		/* find partition type */
		default_desc.sys_type[0] = PARTITION_UNUSED;
		for (i = 0; i < PARTITION_COUNT; ++i) {
			int             s_cyl_sec;
			int             e_cyl_sec;

			s_cyl_sec =
			la_to_u_2_byte(disk_mbr.partition[i].s_cyl_sec);
			e_cyl_sec =
			la_to_u_2_byte(disk_mbr.partition[i].e_cyl_sec);

			if (disk_mbr.partition[i].type != PARTITION_UNUSED) {
				if (default_desc.sys_type[0] !=
							PARTITION_UNUSED) {
#ifdef	USE_LIBSCHILY
					comerrno(EX_BAD,
					"Boot image '%s' has multiple partitions.\n",
							de->whole_name);
#else
					fprintf(stderr,
					"Boot image '%s' has multiple partitions.\n",
							de->whole_name);
					exit(1);
#endif
				}
				default_desc.sys_type[0] =
						disk_mbr.partition[i].type;

			/* a few simple sanity warnings */
				if (!not_bootable &&
				    disk_mbr.partition[i].status !=
							PARTITION_ACTIVE) {
					fprintf(stderr,
					"Warning: partition not marked active.\n");
				}
				if (MBR_CYLINDER(s_cyl_sec) != 0 ||
					disk_mbr.partition[i].s_head != 1 ||
					MBR_SECTOR(s_cyl_sec != 1)) {
					fprintf(stderr,
					"Warning: partition does not start at 0/1/1.\n");
				}
				geosec = (MBR_CYLINDER(e_cyl_sec) + 1) *
					(disk_mbr.partition[i].e_head + 1) *
					MBR_SECTOR(e_cyl_sec);
				if (geosec != nsectors) {
					fprintf(stderr,
					"Warning: image size does not match geometry (%d)\n",
						geosec);
				}
#ifdef DEBUG_TORITO
				fprintf(stderr, "Partition start %u/%u/%u\n",
					MBR_CYLINDER(s_cyl_sec),
					disk_mbr.partition[i].s_head,
					MBR_SECTOR(s_cyl_sec));
				fprintf(stderr, "Partition end %u/%u/%u\n",
					MBR_CYLINDER(e_cyl_sec),
					disk_mbr.partition[i].e_head,
					MBR_SECTOR(e_cyl_sec));
#endif
			}
		}
		if (default_desc.sys_type[0] == PARTITION_UNUSED) {
#ifdef	USE_LIBSCHILY
			comerrno(EX_BAD,
					"Boot image '%s' has no partitions.\n",
							de->whole_name);
#else
			fprintf(stderr,
					"Boot image '%s' has no partitions.\n",
							 de->whole_name);
			exit(1);
#endif
		}
#ifdef DEBUG_TORITO
		fprintf(stderr, "Partition type %u\n",
						default_desc.sys_type[0]);
#endif
	/* load single boot sector, in this case the MBR */
		nsectors = 1;

	} else if (no_emul_boot) {
		/*
		 * no emulation is a simple image boot of all the sectors
		 * in the boot image
		 */
		default_desc.boot_media[0] = EL_TORITO_MEDIA_NOEMUL;
		fprintf(stderr, "No emulation\n");

	} else {
		/* choose size of emulated floppy based on boot image size */
		if (nsectors == 2880) {
			default_desc.boot_media[0] = EL_TORITO_MEDIA_144FLOP;
			fprintf(stderr, "Emulating a 1.44 meg floppy\n");

		} else if (nsectors == 5760) {
			default_desc.boot_media[0] = EL_TORITO_MEDIA_288FLOP;
			fprintf(stderr, "Emulating a 2.88 meg floppy\n");

		} else if (nsectors == 2400) {
			default_desc.boot_media[0] = EL_TORITO_MEDIA_12FLOP;
			fprintf(stderr, "Emulating a 1.2 meg floppy\n");

		} else {
#ifdef	USE_LIBSCHILY
			comerrno(EX_BAD,
			"Error - boot image '%s' is not the an allowable size.\n",
							de->whole_name);
#else
			fprintf(stderr,
			"Error - boot image '%s' is not the an allowable size.\n",
							de->whole_name);
			exit(1);
#endif
		}

		/* load single boot sector for floppies */
		nsectors = 1;
	}

	/* fill in boot image details */
#ifdef DEBUG_TORITO
	fprintf(stderr, "Boot %u sectors\n", nsectors);
	fprintf(stderr, "Extent of boot images is %d\n",
				get_733(de->isorec.extent));
#endif
	set_721(default_desc.nsect, (unsigned int) nsectors);
	set_731(default_desc.bootoff,
		(unsigned int) get_733(de->isorec.extent));

	/* now write it to the virtual boot catalog */
	memcpy(de2->table, &valid_desc, 32);
	memcpy(de2->table + 32, &default_desc, 32);


	/* If the user has asked for it, patch the boot image */
	if (boot_info_table) {
		int             bootimage;
		unsigned int    bi_checksum,
		                total_len;
		static char     csum_buffer[SECTOR_SIZE];
		int             len;
		struct mkisofs_boot_info bi_table;

		bootimage = open(de->whole_name, O_RDWR | O_BINARY);
		if (bootimage == -1) {
#ifdef	USE_LIBSCHILY
			comerr(
			"Error opening boot image file '%s' for update.\n",
							de->whole_name);
#else
			fprintf(stderr,
			"Error opening boot image file '%s' for update.\n",
							de->whole_name);
			perror("");
			exit(1);
#endif
		}
	/* Compute checksum of boot image, sans 64 bytes */
		total_len = 0;
		bi_checksum = 0;
		while ((len = read(bootimage, csum_buffer, SECTOR_SIZE)) > 0) {
			if (total_len & 3) {
#ifdef	USE_LIBSCHILY
				comerrno(EX_BAD,
				"Odd alignment at non-end-of-file in boot image '%s'.\n",
							de->whole_name);
#else
				fprintf(stderr,
				"Odd alignment at non-end-of-file in boot image '%s'.\n",
							de->whole_name);
				exit(1);
#endif
			}
			if (total_len < 64)
				memset(csum_buffer, 0, 64 - total_len);
			if (len < SECTOR_SIZE)
				memset(csum_buffer + len, 0, SECTOR_SIZE-len);
			for (i = 0; i < SECTOR_SIZE; i += 4)
				bi_checksum += get_731(&csum_buffer[i]);
			total_len += len;
		}

		if (total_len != de->size) {
#ifdef	USE_LIBSCHILY
			comerrno(EX_BAD,
			"Boot image file '%s' changed underneath us!\n",
						de->whole_name);
#else
			fprintf(stderr,
			"Boot image file '%s' changed underneath us!\n",
						de->whole_name);
			exit(1);
#endif
		}
		/* End of file, set position to byte 8 */
		lseek(bootimage, 8, SEEK_SET);
		memset(&bi_table, 0, sizeof(bi_table));
		/* Is it always safe to assume PVD is at session_start+16? */
		set_731(bi_table.bi_pvd, session_start + 16);
		set_731(bi_table.bi_file, de->starting_block);
		set_731(bi_table.bi_length, de->size);
		set_731(bi_table.bi_csum, bi_checksum);

		write(bootimage, &bi_table, sizeof(bi_table));
		close(bootimage);
	}
}/* get_torito_desc(... */

/*
 * Function to write the EVD for the disc.
 */
static int
tvd_write(outfile)
	FILE	*outfile;
{
	/* check the boot image is not NULL */
	if (!boot_image) {
#ifdef	USE_LIBSCHILY
		comerrno(EX_BAD, "No boot image specified.\n");
#else
		fprintf(stderr, "No boot image specified.\n");
		exit(1);
#endif
	}
	/* Next we write out the boot volume descriptor for the disc */
	get_torito_desc(&gboot_desc);
	xfwrite(&gboot_desc, 1, SECTOR_SIZE, outfile);
	last_extent_written++;
	return 0;
}

struct output_fragment torito_desc = {NULL, oneblock_size, NULL, tvd_write};