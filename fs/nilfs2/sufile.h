/*
 * sufile.h - NILFS segment usage file.
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

#ifndef _NILFS_SUFILE_H
#define _NILFS_SUFILE_H

#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/nilfs2_fs.h>
#include "mdt.h"

static inline int
nilfs_sufile_ext_supported(const struct inode *sufile)
{
	return NILFS_MDT(sufile)->mi_entry_size >= NILFS_EXT_SEGMENT_USAGE_SIZE;
}

static inline unsigned long nilfs_sufile_get_nsegments(struct inode *sufile)
{
	return ((struct the_nilfs *)sufile->i_sb->s_fs_info)->ns_nsegments;
}

unsigned long nilfs_sufile_get_ncleansegs(struct inode *sufile);

int nilfs_sufile_set_alloc_range(struct inode *sufile, __u64 start, __u64 end);
int nilfs_sufile_alloc(struct inode *, __u64 *);
int nilfs_sufile_mark_dirty(struct inode *sufile, __u64 segnum);
int nilfs_sufile_set_segment_usage(struct inode *sufile, __u64 segnum,
				   unsigned long nblocks, time_t modtime);
int nilfs_sufile_get_stat(struct inode *, struct nilfs_sustat *);
ssize_t nilfs_sufile_get_suinfo(struct inode *, __u64, void *, unsigned,
				size_t);
ssize_t nilfs_sufile_set_suinfo(struct inode *, void *, unsigned , size_t);

int nilfs_sufile_updatev(struct inode *, void *, size_t, size_t, size_t, int,
			 size_t *, void (*dofunc)(struct inode *, void *,
						  struct buffer_head *,
						  struct buffer_head *));
int nilfs_sufile_update(struct inode *, void *, size_t, int,
			void (*dofunc)(struct inode *, void *,
				       struct buffer_head *,
				       struct buffer_head *));
void nilfs_sufile_do_scrap(struct inode *, __u64 *, struct buffer_head *,
			   struct buffer_head *);
void nilfs_sufile_do_free(struct inode *, __u64 *, struct buffer_head *,
			  struct buffer_head *);
void nilfs_sufile_do_cancel_free(struct inode *, __u64 *, struct buffer_head *,
				 struct buffer_head *);
void nilfs_sufile_do_set_error(struct inode *, __u64 *, struct buffer_head *,
			       struct buffer_head *);

int nilfs_sufile_resize(struct inode *sufile, __u64 newnsegs);
int nilfs_sufile_read(struct super_block *sb, size_t susize,
		      struct nilfs_inode *raw_inode, struct inode **inodep);
int nilfs_sufile_trim_fs(struct inode *sufile, struct fstrim_range *range);

/**
 * nilfs_sufile_scrap - make a segment garbage
 * @sufile: inode of segment usage file
 * @segnum: segment number to be freed
 */
static inline int nilfs_sufile_scrap(struct inode *sufile, __u64 segnum)
{
	return nilfs_sufile_update(sufile, &segnum, 0, 1,
				   (void *)nilfs_sufile_do_scrap);
}

/**
 * nilfs_sufile_free - free segment
 * @sufile: inode of segment usage file
 * @segnum: segment number to be freed
 */
static inline int nilfs_sufile_free(struct inode *sufile, __u64 segnum)
{
	return nilfs_sufile_update(sufile, &segnum, 0, 0,
				   (void *)nilfs_sufile_do_free);
}

/**
 * nilfs_sufile_freev - free segments
 * @sufile: inode of segment usage file
 * @segnumv: array of segment numbers
 * @nsegs: size of @segnumv array
 * @ndone: place to store the number of freed segments
 */
static inline int nilfs_sufile_freev(struct inode *sufile, __u64 *segnumv,
				     size_t nsegs, size_t *ndone)
{
	return nilfs_sufile_updatev(sufile, segnumv, sizeof(__u64), 0, nsegs,
				    0, ndone, (void *)nilfs_sufile_do_free);
}

/**
 * nilfs_sufile_cancel_freev - reallocate freeing segments
 * @sufile: inode of segment usage file
 * @segnumv: array of segment numbers
 * @nsegs: size of @segnumv array
 * @ndone: place to store the number of cancelled segments
 *
 * Return Value: On success, 0 is returned. On error, a negative error codes
 * is returned.
 */
static inline int nilfs_sufile_cancel_freev(struct inode *sufile,
					    __u64 *segnumv, size_t nsegs,
					    size_t *ndone)
{
	return nilfs_sufile_updatev(sufile, segnumv, sizeof(__u64), 0, nsegs,
				    0, ndone,
				    (void *)nilfs_sufile_do_cancel_free);
}

/**
 * nilfs_sufile_set_error - mark a segment as erroneous
 * @sufile: inode of segment usage file
 * @segnum: segment number
 *
 * Description: nilfs_sufile_set_error() marks the segment specified by
 * @segnum as erroneous. The error segment will never be used again.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-EINVAL - Invalid segment usage number.
 */
static inline int nilfs_sufile_set_error(struct inode *sufile, __u64 segnum)
{
	return nilfs_sufile_update(sufile, &segnum, 0, 0,
				   (void *)nilfs_sufile_do_set_error);
}

#define NILFS_SUFILE_MC_SIZE_DEFAULT	5
#define NILFS_SUFILE_MC_SIZE_EXT	10

/**
 * struct nilfs_sufile_mod - segment usage modification
 * @m_segnum: segment number
 * @m_value: signed value that gets added to respective segusg field
 */
struct nilfs_sufile_mod {
	__u64 m_segnum;
	__s64 m_value;
};

/**
 * struct nilfs_sufile_mod_cache - segment usage modification cache
 * @mc_mods: array of modifications to segments
 * @mc_capacity: maximum number of elements that fit in @mc_mods
 * @mc_size: number of elements currently filled with valid data
 */
struct nilfs_sufile_mod_cache {
	struct nilfs_sufile_mod *mc_mods;
	size_t mc_capacity;
	size_t mc_size;
};

int nilfs_sufile_mc_init(struct nilfs_sufile_mod_cache *, size_t);

/**
 * nilfs_sufile_mc_destroy - destroy segusg modification cache
 * @mc: modification cache
 *
 * Description: Releases the memory allocated by nilfs_sufile_mc_init and
 * sets the size and capacity to 0. @mc should not be used after a call to
 * this function.
 */
static inline void nilfs_sufile_mc_destroy(struct nilfs_sufile_mod_cache *mc)
{
	if (mc) {
		kfree(mc->mc_mods);
		mc->mc_capacity = 0;
		mc->mc_size = 0;
	}
}

#endif	/* _NILFS_SUFILE_H */
