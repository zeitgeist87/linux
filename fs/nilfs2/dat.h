/*
 * dat.h - NILFS disk address translation.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 */

#ifndef _NILFS_DAT_H
#define _NILFS_DAT_H

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>


struct nilfs_palloc_req;

int nilfs_dat_translate(struct inode *, __u64, sector_t *);

int nilfs_dat_prepare_alloc(struct inode *, struct nilfs_palloc_req *);
void nilfs_dat_commit_alloc(struct inode *, struct nilfs_palloc_req *);
void nilfs_dat_abort_alloc(struct inode *, struct nilfs_palloc_req *);
int nilfs_dat_prepare_start(struct inode *, struct nilfs_palloc_req *);
void nilfs_dat_commit_start(struct inode *, struct nilfs_palloc_req *,
			    sector_t);
int nilfs_dat_prepare_end(struct inode *, struct nilfs_palloc_req *);
void nilfs_dat_commit_end(struct inode *, struct nilfs_palloc_req *, int);
void nilfs_dat_abort_end(struct inode *, struct nilfs_palloc_req *);
int nilfs_dat_prepare_update(struct inode *, struct nilfs_palloc_req *,
			     struct nilfs_palloc_req *);
void nilfs_dat_commit_update(struct inode *, struct nilfs_palloc_req *,
			     struct nilfs_palloc_req *, int);
void nilfs_dat_abort_update(struct inode *, struct nilfs_palloc_req *,
			    struct nilfs_palloc_req *);

int nilfs_dat_mark_dirty(struct inode *, __u64);
int nilfs_dat_freev(struct inode *, __u64 *, size_t);
int nilfs_dat_move(struct inode *, __u64, sector_t);
int nilfs_dat_clean_snapshot_flag(struct inode *, __u64);
ssize_t nilfs_dat_get_vinfo(struct inode *, void *, unsigned, size_t);

int nilfs_dat_read(struct super_block *sb, size_t entry_size,
		   struct nilfs_inode *raw_inode, struct inode **inodep);
void nilfs_dat_do_scan_dec(struct inode *, struct nilfs_palloc_req *, void *);
void nilfs_dat_do_scan_inc(struct inode *, struct nilfs_palloc_req *, void *);

/**
 * nilfs_dat_scan_dec_ss - scan all dat entries for a checkpoint dec suinfo
 * @dat: inode of dat file
 * @cno: snapshot number
 * @prev: previous snapshot number
 * @next: next snapshot number
 */
static inline int nilfs_dat_scan_dec_ss(struct inode *dat, __u64 cno,
					__u64 prev, __u64 next)
{
	__u64 data[3] = { cno, prev, next };
	return nilfs_palloc_scan_entries(dat, nilfs_dat_do_scan_dec, data);
}

/**
 * nilfs_dat_scan_dec_ss - scan all dat entries for a checkpoint inc suinfo
 * @dat: inode of dat file
 * @cno: snapshot number
 */
static inline int nilfs_dat_scan_inc_ss(struct inode *dat, __u64 cno)
{
	return nilfs_palloc_scan_entries(dat, nilfs_dat_do_scan_inc, &cno);
}

#endif	/* _NILFS_DAT_H */
