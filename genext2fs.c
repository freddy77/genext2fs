/* vi: set sw=8 ts=8: */
// genext2fs.c
//
// ext2 filesystem generator for embedded systems
// Copyright (C) 2000 Xavier Bestel <xavier.bestel@free.fr>
//
// Please direct support requests to genext2fs-devel@lists.sourceforge.net
//
// 'du' portions taken from coreutils/du.c in busybox:
//	Copyright (C) 1999,2000 by Lineo, inc. and John Beppu
//	Copyright (C) 1999,2000,2001 by John Beppu <beppu@codepoet.org>
//	Copyright (C) 2002  Edward Betts <edward@debian.org>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; version
// 2 of the License.
//
// Changes:
// 	 3 Jun 2000	Initial release
// 	 6 Jun 2000	Bugfix: fs size multiple of 8
// 			Bugfix: fill blocks with inodes
// 	14 Jun 2000	Bugfix: bad chdir() with -d option
// 			Bugfix: removed size=8n constraint
// 			Changed -d file to -f file
// 			Added -e option
// 	22 Jun 2000	Changed types for 64bits archs
// 	24 Jun 2000	Added endianness swap
// 			Bugfix: bad dir name lookup
// 	03 Aug 2000	Bugfix: ind. blocks endian swap
// 	09 Aug 2000	Bugfix: symlinks endian swap
// 	01 Sep 2000	Bugfix: getopt returns int, not char	proski@gnu.org
// 	10 Sep 2000	Bugfix: device nodes endianness		xavier.gueguen@col.bsf.alcatel.fr
// 			Bugfix: getcwd values for Solaris	xavier.gueguen@col.bsf.alcatel.fr
// 			Bugfix: ANSI scanf for non-GNU C	xavier.gueguen@col.bsf.alcatel.fr
// 	28 Jun 2001	Bugfix: getcwd differs for Solaris/GNU	mike@sowbug.com
// 	 8 Mar 2002	Bugfix: endianness swap of x-indirects
// 	23 Mar 2002	Bugfix: test for IFCHR or IFBLK was flawed
// 	10 Oct 2002	Added comments,makefile targets,	vsundar@ixiacom.com    
// 			endianess swap assert check.  
// 			Copyright (C) 2002 Ixia communications
// 	12 Oct 2002	Added support for triple indirection	vsundar@ixiacom.com
// 			Copyright (C) 2002 Ixia communications
// 	14 Oct 2002	Added support for groups		vsundar@ixiacom.com
// 			Copyright (C) 2002 Ixia communications
// 	 5 Jan 2003	Bugfixes: reserved inodes should be set vsundar@usc.edu
// 			only in the first group; directory names
// 			need to be null padded at the end; and 
// 			number of blocks per group should be a 
// 			multiple of 8. Updated md5 values. 
// 	 6 Jan 2003	Erik Andersen <andersee@debian.org> added
// 			mkfs.jffs2 compatible device table support,
// 			along with -q, -P, -U


#include <config.h>
#include <stdio.h>

#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#if MAJOR_IN_MKDEV
# include <sys/mkdev.h>
#elif MAJOR_IN_SYSMACROS
# include <sys/sysmacros.h>
#endif

#if HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#if STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# if HAVE_STDLIB_H
#  include <stdlib.h>
# endif
# if HAVE_STDDEF_H
#  include <stddef.h>
# endif
#endif

#if HAVE_STRING_H
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif

#if HAVE_STRINGS_H
# include <strings.h>
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#if HAVE_LIBGEN_H
# include <libgen.h>
#endif

#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>

#if HAVE_FCNTL_H
# include <fcntl.h>
#endif

#if HAVE_GETOPT_H
# include <getopt.h>
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#endif

#include <ext2fs/ext2fs.h>

// inode block size (why is it != blocksize ?!?)
/* The field i_blocks in the ext2 inode stores the number of data blocks
   but in terms of 512 bytes. That is what INODE_BLOCKSIZE represents.
   INOBLK is the number of such blocks in an actual disk block            */

#define INODE_BLOCKSIZE   512
#define INOBLK            (fs->blocksize / INODE_BLOCKSIZE)

// reserved inodes

#define EXT2_BAD_INO         1     // Bad blocks inode
#define EXT2_ROOT_INO        2     // Root inode
#define EXT2_ACL_IDX_INO     3     // ACL inode
#define EXT2_ACL_DATA_INO    4     // ACL inode
#define EXT2_BOOT_LOADER_INO 5     // Boot loader inode
#define EXT2_UNDEL_DIR_INO   6     // Undelete directory inode

// file modes

#define FM_IFMT    0170000	// format mask
#define FM_IFSOCK  0140000	// socket
#define FM_IFLNK   0120000	// symbolic link
#define FM_IFREG   0100000	// regular file

#define FM_IFBLK   0060000	// block device
#define FM_IFDIR   0040000	// directory
#define FM_IFCHR   0020000	// character device
#define FM_IFIFO   0010000	// fifo

#define FM_IMASK   0007777	// *all* perms mask for everything below

#define FM_ISUID   0004000	// SUID
#define FM_ISGID   0002000	// SGID
#define FM_ISVTX   0001000	// sticky bit

#define FM_IRWXU   0000700	// entire "user" mask
#define FM_IRUSR   0000400	// read
#define FM_IWUSR   0000200	// write
#define FM_IXUSR   0000100	// execute

#define FM_IRWXG   0000070	// entire "group" mask
#define FM_IRGRP   0000040	// read
#define FM_IWGRP   0000020	// write
#define FM_IXGRP   0000010	// execute

#define FM_IRWXO   0000007	// entire "other" mask
#define FM_IROTH   0000004	// read
#define FM_IWOTH   0000002	// write
#define FM_IXOTH   0000001	// execute

/* Defines for accessing group details */

// Number of groups in the filesystem
#define GRP_NBGROUPS(fs) \
	(((fs)->super->s_blocks_count - fs->super->s_first_data_block + \
	  (fs)->super->s_blocks_per_group - 1) / (fs)->super->s_blocks_per_group)

// Get group inode bitmap (ibm) given the group number
//#define GRP_GET_GROUP_IBM(fs,grp) ( get_blk((fs),(fs)->gd[(grp)].bg_inode_bitmap) )
		
// Given an inode number find the group it belongs to
#define GRP_GROUP_OF_INODE(fs,nod) ( ((nod)-1) / (fs)->sb.s_inodes_per_group)

//Given an inode number get the inode bitmap that covers it
#define GRP_GET_INODE_BITMAP(fs,nod) \
	( GRP_GET_GROUP_IBM((fs),GRP_GROUP_OF_INODE((fs),(nod))) )

//Given an inode number find its offset within the inode bitmap that covers it
#define GRP_IBM_OFFSET(fs,nod) \
	( (nod) - GRP_GROUP_OF_INODE((fs),(nod))*(fs)->sb.s_inodes_per_group )


// the GNU C library has a wonderful scanf("%as", string) which will
// allocate the string with the right size, good to avoid buffer
// overruns. the following macros use it if available or use a
// hacky workaround
// moreover it will define a snprintf() like a sprintf(), i.e.
// without the buffer overrun checking, to work around bugs in
// older solaris. Note that this is still not very portable, in that
// the return value cannot be trusted.

#if SCANF_CAN_MALLOC
# define SCANF_PREFIX "a"
# define SCANF_STRING(s) (&s)
#else
# define SCANF_PREFIX "511"
# define SCANF_STRING(s) (s = malloc(512))
#endif /* SCANF_CAN_MALLOC */

#if PREFER_PORTABLE_SNPRINTF
static inline int
portable_snprintf(char *str, size_t n, const char *fmt, ...)
{
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vsprintf(str, fmt, ap);
	va_end(ap);
	return ret;
}
# define SNPRINTF portable_snprintf
#else
# define SNPRINTF snprintf
#endif /* PREFER_PORTABLE_SNPRINTF */

#if !HAVE_GETLINE
// getline() replacement for Darwin and Solaris etc.
// This code uses backward seeks (unless rchunk is set to 1) which can't work
// on pipes etc. However, add2fs_from_file() only calls getline() for
// regular files, so a larger rchunk and backward seeks are okay.

ssize_t 
getdelim(char **lineptr, size_t *n, int delim, FILE *stream)
{
	char *p;                    // reads stored here
	size_t const rchunk = 512;  // number of bytes to read
	size_t const mchunk = 512;  // number of extra bytes to malloc
	size_t m = rchunk + 1;      // initial buffer size
	
	if (*lineptr) {
		if (*n < m) {
			*lineptr = (char*)realloc(*lineptr, m);
			if (!*lineptr) return -1;
			*n = m;
		}
	} else {
		*lineptr = (char*)malloc(m);
		if (!*lineptr) return -1;
		*n = m;
	}

	m = 0; // record length including seperator

	do {
		size_t i;     // number of bytes read etc
		size_t j = 0; // number of bytes searched

		p = *lineptr + m;

		i = fread(p, 1, rchunk, stream);
		if (i < rchunk && ferror(stream))
			return -1;
		while (j < i) {
			++j;
			if (*p++ == (char)delim) {
				*p = '\0';
				if (j != i) {
					if (fseek(stream, j - i, SEEK_CUR))
						return -1;
					if (feof(stream))
						clearerr(stream);
				}
				m += j;
				return m;
			}
		}

		m += j;
		if (feof(stream)) {
			if (m) return m;
			if (!i) return -1;
		}

		// allocate space for next read plus possible null terminator
		i = ((m + (rchunk + 1 > mchunk ? rchunk + 1 : mchunk) +
		      mchunk - 1) / mchunk) * mchunk;
		if (i != *n) {
			*lineptr = (char*)realloc(*lineptr, i);
			if (!*lineptr) return -1;
			*n = i;
		}
	} while (1);
}
#define getline(a,b,c) getdelim(a,b,'\n',c)
#endif /* HAVE_GETLINE */

// Convert a numerical string to a float, and multiply the result by an
// IEC or SI multiplier if provided; supported multipliers are Ki, Mi, Gi, k, M
// and G.

float
SI_atof(const char *nptr)
{
	float f = 0;
	float m = 1;
	char *suffixptr;

#if HAVE_STRTOF
	f = strtof(nptr, &suffixptr);
#else
	f = (float)strtod(nptr, &suffixptr);
#endif /* HAVE_STRTOF */

	if (*suffixptr) {
		if (!strcmp(suffixptr, "Ki"))
			m = 1 << 10;
		else if (!strcmp(suffixptr, "Mi"))
			m = 1 << 20;
		else if (!strcmp(suffixptr, "Gi"))
			m = 1 << 30;
		else if (!strcmp(suffixptr, "k"))
			m = 1000;
		else if (!strcmp(suffixptr, "M"))
			m = 1000 * 1000;
		else if (!strcmp(suffixptr, "G"))
			m = 1000 * 1000 * 1000;
	}
	return f * m;
}


/* Filesystem structure that support groups */
typedef struct struct_ext2_filsys filesystem;

#define HDLINK_CNT   16
static int32_t hdlink_cnt = HDLINK_CNT;
struct hdlink_s
{
	uint32_t	src_inode;
	uint32_t	dst_nod;
};

struct hdlinks_s 
{
	int32_t count;
	struct hdlink_s *hdl;
};

static struct hdlinks_s hdlinks;

static char * app_name;
static const char *const memory_exhausted = "memory exhausted";

static void
debugf(int line, const char *fmt, ...)
{
	va_list p;
	va_start(p, fmt);
	fflush(stdout);
	fprintf(stderr, "line %d: ", line);
	vfprintf(stderr, fmt, p);
	putc('\n', stderr);
	va_end(p);
}

#define debugf(args...) debugf(__LINE__, args)

// error (un)handling
static void
verror_msg(const char *s, va_list p)
{
	fflush(stdout);
	fprintf(stderr, "%s: ", app_name);
	vfprintf(stderr, s, p);
}
static void
error_msg(const char *s, ...)
{
	va_list p;
	va_start(p, s);
	verror_msg(s, p);
	va_end(p);
	putc('\n', stderr);
}

static void
error_msg_and_die(const char *s, ...)
{
	va_list p;
	va_start(p, s);
	verror_msg(s, p);
	va_end(p);
	putc('\n', stderr);
	exit(EXIT_FAILURE);
}

static void
vperror_msg(const char *s, va_list p)
{
	int err = errno;
	if (s == 0)
		s = "";
	verror_msg(s, p);
	if (*s)
		s = ": ";
	fprintf(stderr, "%s%s\n", s, strerror(err));
}

static void
perror_msg_and_die(const char *s, ...)
{
	va_list p;
	va_start(p, s);
	vperror_msg(s, p);
	va_end(p);
	exit(EXIT_FAILURE);
}

static FILE *
xfopen(const char *path, const char *mode)
{
	FILE *fp;
	if ((fp = fopen(path, mode)) == NULL)
		perror_msg_and_die("%s", path);
	return fp;
}

static char *
xstrdup(const char *s)
{
	char *t;

	if (s == NULL)
		return NULL;
	t = strdup(s);
	if (t == NULL)
		error_msg_and_die(memory_exhausted);
	return t;
}

static void *
xrealloc(void *ptr, size_t size)
{
	if (!ptr)
		ptr = malloc(size);
	else
		ptr = realloc(ptr, size);
	if (ptr == NULL && size != 0)
		error_msg_and_die(memory_exhausted);
	return ptr;
}

static char *
xreadlink(const char *path)
{
	static const int GROWBY = 80; /* how large we will grow strings by */

	char *buf = NULL;
	int bufsize = 0, readsize = 0;

	do {
		buf = xrealloc(buf, bufsize += GROWBY);
		readsize = readlink(path, buf, bufsize); /* 1st try */
		if (readsize == -1) {
			perror_msg_and_die("%s:%s", app_name, path);
		}
	}
	while (bufsize < readsize + 1);

	buf[readsize] = '\0';
	return buf;
}

static int
is_hardlink(ino_t inode)
{
	int i;

	for(i = 0; i < hdlinks.count; i++) {
		if(hdlinks.hdl[i].src_inode == inode)
			return i;
	}
	return -1;		
}

// printf helper macro
#define plural(a) (a), ((a) > 1) ? "s" : ""

// check if something is allocated in the bitmap
static inline uint32_t
allocated(const uint8_t *b, uint32_t item)
{
	return b[(item-1) / 8] & (1 << ((item-1) % 8));
}

#if 0
// return a given block from a filesystem
static inline uint8_t *
get_blk(filesystem *fs, uint32_t blk)
{
	return (uint8_t*)fs + blk*fs->blocksize;
}
#endif

// print a bitmap allocation
static void
print_bm(uint8_t *bmp, uint32_t max)
{
	uint32_t i;
	printf("----+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0\n");
	for(i=1; i <= max; i++)
	{
		putchar(allocated(bmp, i) ? '*' : '.');
		if(!(i % 100))
			printf("\n");
	}
	if((i-1) % 100)
		printf("\n");
}

// link an entry (inode #) to a directory
static void
add2dir(filesystem *fs, ext2_ino_t dnod, ext2_ino_t nod, const char* name)
{
	int rc;
	int flags = 0; // do_modetoext2lag(mode);

	do {
		debugf("calling ext2fs_link(e2fs, %d, %s, %d, %d);", dnod, name, nod, flags);
		rc = ext2fs_link(fs, dnod, name, nod, flags);
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(e2fs, &d)", dnod);
			if (ext2fs_expand_dir(fs, dnod))
				error_msg_and_die("error while expanding directory %d", dnod);
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc)
		error_msg_and_die("ext2fs_link(e2fs, %d, %s, %d, %d); failed", dnod, name, nod, flags);
}

// find an entry in a directory
static ext2_ino_t
find_dir(filesystem *fs, ext2_ino_t nod, const char * name)
{
	ext2_ino_t ino;
	int rc = ext2fs_lookup(fs, nod, name, strlen(name), NULL, &ino);
	if (!rc)
		return ino;

	if (rc != EXT2_ET_FILE_NOT_FOUND)
		error_msg_and_die("ext2fs_lookup failed");

	return 0;
}

// find the inode of a full path
static ext2_ino_t
find_path(filesystem *fs, ext2_ino_t nod, const char * name)
{
	ext2_ino_t ino;
	errcode_t rc;

	rc = ext2fs_namei(fs, EXT2_ROOT_INO, nod, name, &ino);
	return rc ? 0 : ino;
}

static int
do_modetoext2lag (mode_t mode)
{
	if (S_ISREG(mode)) {
		return EXT2_FT_REG_FILE;
	} else if (S_ISDIR(mode)) {
		return EXT2_FT_DIR;
	} else if (S_ISCHR(mode)) {
		return EXT2_FT_CHRDEV;
	} else if (S_ISBLK(mode)) {
		return EXT2_FT_BLKDEV;
	} else if (S_ISFIFO(mode)) {
		return EXT2_FT_FIFO;
	} else if (S_ISSOCK(mode)) {
		return EXT2_FT_SOCK;
	} else if (S_ISLNK(mode)) {
		return EXT2_FT_SYMLINK;
	}
	return EXT2_FT_UNKNOWN;
}

static inline int
old_valid_dev(dev_t dev)
{
	return major(dev) < 256 && minor(dev) < 256;
}

static inline uint16_t
old_encode_dev(dev_t dev)
{
	return (major(dev) << 8) | minor(dev);
}

static inline uint8_t
new_encode_dev(dev_t dev)
{
	unsigned major = major(dev);
	unsigned minor = minor(dev);
	return (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
}

static struct ext2_inode*
get_nod(ext2_filsys e2fs, ext2_ino_t ino, struct ext2_inode* inode)
{
	if (ext2fs_read_inode(e2fs, ino, inode))
		error_msg_and_die("ext2fs_read_inode failed");
	return inode;
}

static ext2_ino_t
do_create (ext2_filsys e2fs,
	const ext2_ino_t ino, /** parent inode */
	const char *name,
	mode_t mode,
	dev_t dev,
	const char *fastsymlink,
	uid_t uid, gid_t gid,
	time_t ctime, time_t mtime)
{
	int rt;
	int is_dir = S_ISDIR(mode);
	errcode_t rc;

	struct ext2_inode inode;
	ext2_ino_t n_ino;

	debugf("enter");
	debugf("name = %s, mode: 0%o", name, mode);

	if (ext2fs_read_inode(e2fs, ino, &inode))
		error_msg_and_die("ext2fs_read_inode failed");

	rc = ext2fs_new_inode(e2fs, ino, mode, 0, &n_ino);
	if (rc)
		error_msg_and_die("ext2fs_new_inode(ep.fs, ino, mode, 0, &n_ino); failed");

	do {
		debugf("calling ext2fs_link(e2fs, %d, %s, %d, %d);", ino, name, n_ino, do_modetoext2lag(mode));
		if (is_dir)
			rc = ext2fs_mkdir(e2fs, ino, n_ino, name);
		else
			rc = ext2fs_link(e2fs, ino, name, n_ino, do_modetoext2lag(mode));
		if (rc == EXT2_ET_DIR_NO_SPACE) {
			debugf("calling ext2fs_expand_dir(e2fs, &d)", ino);
			if (ext2fs_expand_dir(e2fs, ino))
				error_msg_and_die("error while expanding directory %d", ino);
		}
	} while (rc == EXT2_ET_DIR_NO_SPACE);
	if (rc)
		error_msg_and_die("ext2fs_link(e2fs, %d, %s, %d, %d); failed with %d", ino, name, n_ino, do_modetoext2lag(mode), rc);

	if (ext2fs_test_inode_bitmap(e2fs->inode_map, n_ino)) {
		debugf("inode already set");
	}

	if (is_dir) {
		if (ext2fs_read_inode(e2fs, n_ino, &inode))
			error_msg_and_die("ext2fs_read_inode failed");
	} else {
		ext2fs_inode_alloc_stats2(e2fs, n_ino, +1, 0);

		memset(&inode, 0, sizeof(inode));
		inode.i_links_count = 1;
		inode.i_size = 0;
	}
	inode.i_mode = mode;
	inode.i_atime = inode.i_mtime = mtime;
	inode.i_ctime = ctime;
	inode.i_uid = uid;
	inode.i_gid = gid;

	if (S_ISCHR(mode) || S_ISBLK(mode)) {
		if (old_valid_dev(dev))
			inode.i_block[0]= ext2fs_cpu_to_le32(old_encode_dev(dev));
		else
			inode.i_block[1]= ext2fs_cpu_to_le32(new_encode_dev(dev));
	}

	if (S_ISLNK(mode) && fastsymlink != NULL) {
		inode.i_size = strlen(fastsymlink);
		strncpy((char *)&(inode.i_block[0]),fastsymlink,
				(EXT2_N_BLOCKS * sizeof(inode.i_block[0])));
	}

	rc = ext2fs_write_new_inode(e2fs, n_ino, &inode);
	if (rc)
		error_msg_and_die("ext2fs_write_new_inode(e2fs, n_ino, &inode);");

	debugf("leave");
	return n_ino;
}


// create a simple inode
static ext2_ino_t
mknod_fs(filesystem *fs, ext2_ino_t parent_nod, const char *name, uint16_t mode, uid_t uid, gid_t gid, uint8_t major, uint8_t minor, uint32_t ctime, uint32_t mtime)
{
	ext2_ino_t nod = find_dir(fs, parent_nod, name);

	if (nod != 0) 	{
		struct ext2_inode inode;
		if (ext2fs_read_inode(fs, nod, &inode))
			error_msg_and_die("ext2fs_read_inode failed");
		if ((inode.i_mode & FM_IFMT) != (mode & FM_IFMT))
			error_msg_and_die("node '%s' already exists and isn't of the same type", name);
		inode.i_mode = mode;
		inode.i_uid = uid;
		inode.i_gid = gid;
		inode.i_atime = mtime;
		inode.i_ctime = ctime;
		inode.i_mtime = mtime;
		if (ext2fs_write_inode(fs, nod, &inode))
			error_msg_and_die("ext2fs_write_inode failed");
	} else {
		nod = do_create(fs, parent_nod, name, mode, makedev(major, minor), NULL, uid, gid, ctime, mtime);
	}
	return nod;
}

// make a full-fledged directory (i.e. with "." & "..")
static inline ext2_ino_t
mkdir_fs(filesystem *fs, ext2_ino_t parent_nod, const char *name, uint32_t mode,
	uid_t uid, gid_t gid, uint32_t ctime, uint32_t mtime)
{
	return mknod_fs(fs, parent_nod, name, mode|FM_IFDIR, uid, gid, 0, 0, ctime, mtime);
}

#define EXT2_FILE_SHARED_INODE 0x8000

static ext2_file_t
do_open(ext2_filsys e2fs, ext2_ino_t ino, int flags)
{
	errcode_t rc;
	ext2_file_t efile;

	debugf("enter");

	rc = ext2fs_file_open(e2fs, ino,
			(((flags & O_ACCMODE) != 0) ? EXT2_FILE_WRITE : 0) | EXT2_FILE_SHARED_INODE, 
			&efile);
	if (rc)
		return NULL;

	debugf("leave");
	return efile;
}

// TODO support files bigger then 2/4gb
static size_t
do_write(ext2_file_t efile, const char *buf, size_t size, off_t offset)
{
	int rt;
	const char *tmp;
	unsigned int wr;
	unsigned long long npos;
	unsigned long long fsize;
	struct ext2_inode *inode;

	debugf("enter");

	rt = ext2fs_file_llseek(efile, offset, SEEK_SET, &npos);
	if (rt) {
		debugf("ext2fs_file_lseek(efile, %lld, SEEK_SET, &npos); failed", (long long) offset);
		return rt;
	}

	inode = ext2fs_file_get_inode(efile);
	for (rt = 0, wr = 0, tmp = buf; size > 0 && rt == 0; size -= wr, tmp += wr) {
		debugf("size: %lu, written: %lu", (unsigned long) size, (unsigned long) wr);
		rt = ext2fs_file_write(efile, tmp, size, &wr);

		// update size if needed
		offset += wr;
		if (!rt && offset > EXT2_I_SIZE(inode)) {
			inode->i_size = offset & 0xffffffff;
			inode->i_size_high = (offset >> 32) & 0xffffffff;
		}
	}
	if (rt) {
		debugf("ext2fs_file_write(edile, tmp, size, &wr); failed");
		return rt;
	}

	rt = ext2fs_file_flush(efile);
	if (rt) {
		debugf("ext2_file_flush(efile); failed");
		return rt;
	}

	debugf("leave");
	return wr;
}

static int
do_release(ext2_file_t efile)
{
	errcode_t rc;

	rc = ext2fs_file_close(efile);
	if (rc)
		return -EIO;

	return 0;
}


// make a symlink
static ext2_ino_t
mklink_fs(filesystem *e2fs, ext2_ino_t parent_nod, const char *destname, size_t sourcelen, uint8_t *sourcename, uid_t uid, gid_t gid, uint32_t ctime, uint32_t mtime)
{
	int rt;
	size_t wr;
	ext2_file_t efile;
	ext2_ino_t ino;

	debugf("enter");
	debugf("source: %s, dest: %s", sourcename, destname);

	/* a short symlink is stored in the inode (recycling the i_block array) */
	if (sourcelen < (EXT2_N_BLOCKS * sizeof(uint32_t))) {
		ino = do_create(e2fs, parent_nod, destname, LINUX_S_IFLNK | 0777, 0, sourcename, uid, gid, ctime, mtime);
	} else {
		ino = do_create(e2fs, parent_nod, destname, LINUX_S_IFLNK | 0777, 0, NULL, uid, gid, ctime, mtime);

		efile = do_open(e2fs, ino, O_WRONLY);
		if (efile == NULL)
			perror_msg_and_die("do_open(%d); failed", ino);
		wr = do_write(efile, sourcename, sourcelen, 0);
		if (wr != sourcelen)
			perror_msg_and_die("do_write(efile, %s, %d, 0); failed", sourcename, strlen(sourcename) + 1);
		rt = do_release(efile);
		if (rt != 0)
			perror_msg_and_die("do_release(efile); failed");
	}
	debugf("leave");
	return ino;
}

// make a file from a FILE*
static ext2_ino_t
mkfile_fs(filesystem *fs, ext2_ino_t parent_nod, const char *name, uint32_t mode, size_t size, FILE *f, uid_t uid, gid_t gid, uint32_t ctime, uint32_t mtime)
{
	ext2_file_t efile;
	uint8_t * b;
	ext2_ino_t ino = mknod_fs(fs, parent_nod, name, mode|FM_IFREG, uid, gid, 0, 0, ctime, mtime);

	// TODO use fallocate, handle sparse files
	if (size) {
		int rt;
		size_t wr;

		if(!(b = (uint8_t*)calloc(size, 1)))
			error_msg_and_die("not enough mem to read file '%s'", name);
		if(f)
			fread(b, size, 1, f); // FIXME: ugly. use mmap() ...

		efile = do_open(fs, ino, O_WRONLY);
		if (efile == NULL)
			perror_msg_and_die("do_open(%d); failed", ino);
		wr = do_write(efile, b, size, 0);
		if (wr != size)
			perror_msg_and_die("do_write(efile, %p, %d, 0); failed", b, size);
		rt = do_release(efile);
		if (rt != 0)
			perror_msg_and_die("do_release(efile); failed");

		free(b);
	}
	return ino;
}

// retrieves a mode info from a struct stat
static uint32_t
get_mode(struct stat *st)
{
	uint32_t mode = 0;

	if(st->st_mode & S_IRUSR)
		mode |= FM_IRUSR;
	if(st->st_mode & S_IWUSR)
		mode |= FM_IWUSR;
	if(st->st_mode & S_IXUSR)
		mode |= FM_IXUSR;
	if(st->st_mode & S_IRGRP)
		mode |= FM_IRGRP;
	if(st->st_mode & S_IWGRP)
		mode |= FM_IWGRP;
	if(st->st_mode & S_IXGRP)
		mode |= FM_IXGRP;
	if(st->st_mode & S_IROTH)
		mode |= FM_IROTH;
	if(st->st_mode & S_IWOTH)
		mode |= FM_IWOTH;
	if(st->st_mode & S_IXOTH)
		mode |= FM_IXOTH;
	if(st->st_mode & S_ISUID)
		mode |= FM_ISUID;
	if(st->st_mode & S_ISGID)
		mode |= FM_ISGID;
	if(st->st_mode & S_ISVTX)
		mode |= FM_ISVTX;
	return mode;
}

// add or fixup entries to the filesystem from a text file
/*  device table entries take the form of:
    <path>	<type> <mode>	<uid>	<gid>	<major>	<minor>	<start>	<inc>	<count>
    /dev/mem     c    640       0       0         1       1       0     0         -

    type can be one of: 
	f	A regular file
	d	Directory
	c	Character special device file
	b	Block special device file
	p	Fifo (named pipe)

    I don't bother with symlinks (permissions are irrelevant), hard
    links (special cases of regular files), or sockets (why bother).

    Regular files must exist in the target root directory.  If a char,
    block, fifo, or directory does not exist, it will be created.
*/

static void
add2fs_from_file(filesystem *fs, ext2_ino_t this_nod, FILE * fh, uint32_t fs_timestamp)
{
	unsigned long mode, uid, gid, major, minor;
	unsigned long start, increment, count;
	ext2_ino_t nod;
	uint32_t ctime, mtime;
	char *c, type, *path = NULL, *path2 = NULL, *dir, *name, *line = NULL;
	size_t len;
	struct stat st;
	int nbargs, lineno = 0;

	fstat(fileno(fh), &st);
	ctime = fs_timestamp;
	mtime = st.st_mtime;
	while(getline(&line, &len, fh) >= 0)
	{
		mode = uid = gid = major = minor = 0;
		start = 0; increment = 1; count = 0;
		lineno++;
		if((c = strchr(line, '#')))
			*c = 0;
		if (path) {
			free(path);
			path = NULL;
		}
		if (path2) {
			free(path2);
			path2 = NULL;
		}
		nbargs = sscanf (line, "%" SCANF_PREFIX "s %c %lo %lu %lu %lu %lu %lu %lu %lu",
					SCANF_STRING(path), &type, &mode, &uid, &gid, &major, &minor,
					&start, &increment, &count);
		if(nbargs < 3)
		{
			if(nbargs > 0)
				error_msg("device table line %d skipped: bad format for entry '%s'", lineno, path);
			continue;
		}
		mode &= FM_IMASK;
		path2 = strdup(path);
		name = basename(path);
		dir = dirname(path2);
		if((!strcmp(name, ".")) || (!strcmp(name, "..")))
		{
			error_msg("device table line %d skipped", lineno);
			continue;
		}
		if(fs)
		{
			if(!(nod = find_path(fs, this_nod, dir)))
			{
				error_msg("device table line %d skipped: can't find directory '%s' to create '%s''", lineno, dir, name);
				continue;
			}
		}
		else
			nod = 0;
		switch (type)
		{
			case 'd':
				mode |= FM_IFDIR;
				break;
			case 'f':
				mode |= FM_IFREG;
				break;
			case 'p':
				mode |= FM_IFIFO;
				break;
			case 's':
				mode |= FM_IFSOCK;
				break;
			case 'c':
				mode |= FM_IFCHR;
				break;
			case 'b':
				mode |= FM_IFBLK;
				break;
			default:
				error_msg("device table line %d skipped: bad type '%c' for entry '%s'", lineno, type, name);
				continue;
		}
		if(count > 0)
		{
			char *dname;
			unsigned long i;
			unsigned len;
			len = strlen(name) + 10;
			dname = malloc(len + 1);
			for(i = start; i < count; i++)
			{
				SNPRINTF(dname, len, "%s%lu", name, i);
				mknod_fs(fs, nod, dname, mode, uid, gid, major, minor + (i * increment - start), ctime, mtime);
			}
			free(dname);
		}
		else
			mknod_fs(fs, nod, name, mode, uid, gid, major, minor, ctime, mtime);
	}
	if (line)
		free(line);
	if (path) 
		free(path);
	if (path2)
		free(path2);
}

// adds a tree of entries to the filesystem from current dir
static void
add2fs_from_dir(filesystem *fs, ext2_ino_t this_nod, int squash_uids, int squash_perms, uint32_t fs_timestamp)
{
	ext2_ino_t nod;
	uint32_t uid, gid, mode, ctime, mtime;
	const char *name;
	FILE *fh;
	DIR *dh;
	struct dirent *dent;
	struct stat st;
	char *lnk;
	ext2_ino_t save_nod;

	if(!(dh = opendir(".")))
		perror_msg_and_die(".");
	while((dent = readdir(dh)))
	{
		if((!strcmp(dent->d_name, ".")) || (!strcmp(dent->d_name, "..")))
			continue;
		lstat(dent->d_name, &st);
		uid = st.st_uid;
		gid = st.st_gid;
		ctime = fs_timestamp;
		mtime = st.st_mtime;
		name = dent->d_name;
		mode = get_mode(&st);
		if(squash_uids)
			uid = gid = 0;
		if(squash_perms)
			mode &= ~(FM_IRWXG | FM_IRWXO);
		save_nod = 0;
		/* Check for hardlinks */
		if (!S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && st.st_nlink > 1) {
			int32_t hdlink = is_hardlink(st.st_ino);
			if (hdlink >= 0) {
				add2dir(fs, this_nod, hdlinks.hdl[hdlink].dst_nod, name);
				continue;
			} else {
				save_nod = 1;
			}
		}
		switch(st.st_mode & S_IFMT)
		{
#if HAVE_STRUCT_STAT_ST_RDEV
			case S_IFCHR:
				nod = mknod_fs(fs, this_nod, name, mode|FM_IFCHR, uid, gid, major(st.st_rdev), minor(st.st_rdev), ctime, mtime);
				break;
			case S_IFBLK:
				nod = mknod_fs(fs, this_nod, name, mode|FM_IFBLK, uid, gid, major(st.st_rdev), minor(st.st_rdev), ctime, mtime);
				break;
#endif
			case S_IFIFO:
				nod = mknod_fs(fs, this_nod, name, mode|FM_IFIFO, uid, gid, 0, 0, ctime, mtime);
				break;
			case S_IFSOCK:
				nod = mknod_fs(fs, this_nod, name, mode|FM_IFSOCK, uid, gid, 0, 0, ctime, mtime);
				break;
			case S_IFLNK:
				lnk = xreadlink(dent->d_name);
				mklink_fs(fs, this_nod, name, st.st_size, (uint8_t*)lnk, uid, gid, ctime, mtime);
				free(lnk);
				break;
			case S_IFREG:
				fh = xfopen(dent->d_name, "rb");
				nod = mkfile_fs(fs, this_nod, name, mode, st.st_size, fh, uid, gid, ctime, mtime);
				fclose(fh);
				break;
			case S_IFDIR:
				nod = mkdir_fs(fs, this_nod, name, mode, uid, gid, ctime, mtime);
				if(chdir(dent->d_name) < 0)
					perror_msg_and_die(name);
				add2fs_from_dir(fs, nod, squash_uids, squash_perms, fs_timestamp);
				chdir("..");
				break;
			default:
				error_msg("ignoring entry %s", name);
		}
		if (save_nod) {
			if (hdlinks.count == hdlink_cnt) {
				hdlinks.hdl =
					xrealloc (hdlinks.hdl, (hdlink_cnt + HDLINK_CNT) *
							  sizeof (struct hdlink_s));
				hdlink_cnt += HDLINK_CNT;
			}
			hdlinks.hdl[hdlinks.count].src_inode = st.st_ino;
			hdlinks.hdl[hdlinks.count].dst_nod = nod;
			hdlinks.count++;
		}
	}
	closedir(dh);
}

// loads a filesystem from disk
static ext2_filsys
load_fs(const char *device, int readonly)
{
	errcode_t rc;
	ext2_filsys e2fs = NULL;

	rc = ext2fs_open(device, 
			readonly ? 0 : EXT2_FLAG_RW,
			0, 0, unix_io_manager, &e2fs);
	if (rc)
		perror_msg_and_die("error opening filesystem image");

	if (readonly != 1)
		rc = ext2fs_read_bitmaps(e2fs);
	if (rc) {
		ext2fs_close(e2fs);
		perror_msg_and_die("Error while reading bitmaps");
	}
	debugf("FileSystem %s", (e2fs->flags & EXT2_FLAG_RW) ? "Read&Write" : "ReadOnly");

	debugf("leave");

	return e2fs;
}

static void
free_fs(filesystem *e2fs)
{
	if (ext2fs_close(e2fs))
		perror_msg_and_die("Error while trying to close ext2 filesystem");
}

typedef struct {
	FILE *fh;
	unsigned count;
} gen_list_block_data;

static int
list_func(ext2_filsys fs,
	blk64_t     *blocknr,
	e2_blkcnt_t blockcnt,
	blk64_t     ref_blk,
	int         ref_offset,
	void        *priv_data)
{
	gen_list_block_data *data = (gen_list_block_data *) priv_data;
	++data->count;
	fprintf(data->fh, " %lld", (long long) *blocknr);
	return 0;
}

// TODO understand the better setting here
#define LIST_FLAGS BLOCK_FLAG_DATA_ONLY|BLOCK_FLAG_READ_ONLY

// just walk through blocks list
static void
flist_blocks(filesystem *fs, ext2_ino_t nod, FILE *fh)
{
	gen_list_block_data data = { fh, 0 };
	if (ext2fs_block_iterate3(fs, nod, LIST_FLAGS, NULL, list_func, &data))
		error_msg_and_die("error getting block informations");
	fprintf(fh, "\n");
}

// walk through blocks list
static void
list_blocks(filesystem *fs, ext2_ino_t nod)
{
	gen_list_block_data data = { stdout, 0 };
	if (ext2fs_block_iterate3(fs, nod, LIST_FLAGS, NULL, list_func, &data))
		error_msg_and_die("error getting block informations");
	printf("\n%d blocks (%d bytes)\n", data.count, data.count * fs->blocksize);
}

typedef struct {
	FILE *out;
	ext2_ino_t nod;
	int32_t left;
	void *buf;
} gen_write_block_data;

static int
write_func(ext2_filsys fs,
	blk64_t     *blocknr,
	e2_blkcnt_t blockcnt,
	blk64_t     ref_blk,
	int         ref_offset,
	void        *priv_data)
{
	gen_write_block_data *data = (gen_write_block_data *) priv_data;
	int32_t left = data->left;
	if (left <= 0)
		error_msg_and_die("wrong size while saving inode %d", data->nod);
	if (left > fs->blocksize)
		left = fs->blocksize;
	if (io_channel_read_blk64(fs->io, *blocknr, 1, data->buf))
		error_msg_and_die("error reading block");
	if (fwrite(data->buf, left, 1, data->out) != 1)
		error_msg_and_die("error while saving inode %d", data->nod);
	data->left -= left;
	return 0;
}

// saves blocks to FILE*
static void
write_blocks(filesystem *fs, ext2_ino_t nod, FILE* f)
{
	gen_write_block_data data = { f, nod };
	struct ext2_inode inode;

	data.left = get_nod(fs, nod, &inode)->i_size;
	data.buf = xrealloc(NULL, fs->blocksize);
	if (ext2fs_block_iterate3(fs, nod, LIST_FLAGS, NULL, write_func, &data))
		error_msg_and_die("error getting block informations");
	free(data.buf);
}

// print block/char device minor and major
static void
print_dev(filesystem *fs, ext2_ino_t nod)
{
	int minor, major;
	struct ext2_inode inode;

	get_nod(fs, nod, &inode);
	minor = ((uint8_t*)inode.i_block)[0];
	major = ((uint8_t*)inode.i_block)[1];
	printf("major: %d, minor: %d\n", major, minor);
}

static int
dir_iter(ext2_ino_t    dir,
	int   entry,
	struct ext2_dir_entry *d,
	int   offset,
	int   blocksize,
	char  *buf,
	void  *priv_data)
{
	int name_len = d->name_len & 255;
	if (d->inode) {
		printf("entry '%.*s' (inode %d): rec_len: %d (name_len: %d)\n",
			name_len, d->name, d->inode, d->rec_len, name_len);
	}
	return 0;
}

// print an inode as a directory
static void
print_dir(filesystem *fs, ext2_ino_t nod)
{
	printf("directory for inode %d:\n", nod);
	ext2fs_dir_iterate2(fs, nod, 0, NULL, dir_iter, NULL);
}

// print a symbolic link
static void
print_link(filesystem *fs, ext2_ino_t nod)
{
	struct ext2_inode inode;

	get_nod(fs, nod, &inode);
	if(!inode.i_blocks)
		printf("links to '%s'\n", (char*)inode.i_block);
	else
	{
		printf("links to '");
		write_blocks(fs, nod, stdout);
		printf("'\n");
	}
}

// make a ls-like printout of permissions
static void
make_perms(uint32_t mode, char perms[11])
{
	strcpy(perms, "----------");
	if(mode & FM_IRUSR)
		perms[1] = 'r';
	if(mode & FM_IWUSR)
		perms[2] = 'w';
	if(mode & FM_IXUSR)
		perms[3] = 'x';
	if(mode & FM_IRGRP)
		perms[4] = 'r';
	if(mode & FM_IWGRP)
		perms[5] = 'w';
	if(mode & FM_IXGRP)
		perms[6] = 'x';
	if(mode & FM_IROTH)
		perms[7] = 'r';
	if(mode & FM_IWOTH)
		perms[8] = 'w';
	if(mode & FM_IXOTH)
		perms[9] = 'x';
	if(mode & FM_ISUID)
		perms[3] = 's';
	if(mode & FM_ISGID)
		perms[6] = 's';
	if(mode & FM_ISVTX)
		perms[9] = 't';
	switch(mode & FM_IFMT)
	{
		case 0:
			*perms = '0';
			break;
		case FM_IFSOCK:
			*perms = 's';
			break;
		case FM_IFLNK:
			*perms = 'l';
			break;
		case FM_IFREG:
			*perms = '-';
			break;
		case FM_IFBLK:
			*perms = 'b';
			break;
		case FM_IFDIR:
			*perms = 'd';
			break;
		case FM_IFCHR:
			*perms = 'c';
			break;
		case FM_IFIFO:
			*perms = 'p';
			break;
		default:
			*perms = '?';
	}
}

// print an inode
static void
print_inode(filesystem *fs, ext2_ino_t nod)
{
	char *s;
	char perms[11];
	struct ext2_inode inode;
	if(!get_nod(fs, nod, &inode)->i_mode)
		return;
	switch(nod)
	{
		case EXT2_BAD_INO:
			s = "bad blocks";
			break;
		case EXT2_ROOT_INO:
			s = "root";
			break;
		case EXT2_ACL_IDX_INO:
		case EXT2_ACL_DATA_INO:
			s = "ACL";
			break;
		case EXT2_BOOT_LOADER_INO:
			s = "boot loader";
			break;
		case EXT2_UNDEL_DIR_INO:
			s = "undelete directory";
			break;
		default:
			s = (nod >= EXT2_FIRST_INO(fs->super)) ? "normal" : "unknown reserved"; 
	}
	printf("inode %d (%s, %d links): ", nod, s, inode.i_links_count);
// TODO ??
#if 0
	if(!allocated(GRP_GET_INODE_BITMAP(fs,nod), GRP_IBM_OFFSET(fs,nod)))
	{
		printf("unallocated\n");
		return;
	}
#endif
	make_perms(inode.i_mode, perms);
	printf("%s,  size: %lld byte%s (%d block%s)\n", perms, plural((long long) EXT2_I_SIZE(&inode)), plural(inode.i_blocks / INOBLK));
	switch(inode.i_mode & FM_IFMT)
	{
		case FM_IFSOCK:
			list_blocks(fs, nod);
			break;
		case FM_IFLNK:
			print_link(fs, nod);
			break;
		case FM_IFREG:
			list_blocks(fs, nod);
			break;
		case FM_IFBLK:
			print_dev(fs, nod);
			break;
		case FM_IFDIR:
			list_blocks(fs, nod);
			print_dir(fs, nod);
			break;
		case FM_IFCHR:
			print_dev(fs, nod);
			break;
		case FM_IFIFO:
			list_blocks(fs, nod);
			break;
		default:
			list_blocks(fs, nod);
	}
	printf("Done with inode %d\n",nod);
}

static void
get_bmp(struct ext2fs_struct_generic_bitmap *bmap, blk64_t b, uint32_t num, uint8_t **bmp, size_t *size)
{
	uint32_t i;
	uint8_t *p = *bmp;
	size_t s = (num+7)/8;;

	if (s > *size) {
		p = (uint8_t *) xrealloc(p, s);
		*size = s;
		*bmp = p;
	}

	if (ext2fs_get_block_bitmap_range2(bmap, b+1, num, p))
		perror_msg_and_die("error reading bitmap");
}

// describes various fields in a filesystem
static void
print_fs(filesystem *fs)
{
	uint32_t i;
	uint8_t *bmp = NULL;
	size_t bmp_size;

	printf("%d blocks (%d free, %d reserved), first data block: %d\n",
	       fs->super->s_blocks_count, fs->super->s_free_blocks_count,
	       fs->super->s_r_blocks_count, fs->super->s_first_data_block);
	printf("%d inodes (%d free)\n", fs->super->s_inodes_count,
	       fs->super->s_free_inodes_count);
	printf("block size = %d, frag size = %d\n",
	       fs->super->s_log_block_size ? (fs->super->s_log_block_size << 11) : 1024,
	       fs->super->s_log_cluster_size ? (fs->super->s_log_cluster_size << 11) : 1024);
	printf("number of groups: %d\n",GRP_NBGROUPS(fs));
	printf("%d blocks per group,%d frags per group,%d inodes per group\n",
	     fs->super->s_blocks_per_group, fs->super->s_clusters_per_group,
	     fs->super->s_inodes_per_group);
	printf("Size of inode table: %d blocks\n",
		(int)(fs->super->s_inodes_per_group * sizeof(struct ext2_inode) / fs->blocksize));
	for (i = 0; i < GRP_NBGROUPS(fs); i++) {
		struct ext2_group_desc *gd = ext2fs_group_desc(fs, fs->group_desc, i);
		printf("Group No: %d\n", i+1);
		printf("block bitmap: block %d,inode bitmap: block %d, inode table: block %d\n",
		     gd[i].bg_block_bitmap, gd[i].bg_inode_bitmap,
		     gd[i].bg_inode_table);
		printf("block bitmap allocation:\n");
		get_bmp(fs->block_map, i * fs->super->s_blocks_per_group, fs->super->s_blocks_per_group, &bmp, &bmp_size);
		print_bm(bmp, fs->super->s_blocks_per_group);
		printf("inode bitmap allocation:\n");
		get_bmp(fs->inode_map, i * fs->super->s_inodes_per_group, fs->super->s_inodes_per_group, &bmp, &bmp_size);
		print_bm(bmp, fs->super->s_inodes_per_group);
		for (i = 1; i <= fs->super->s_inodes_per_group; i++)
			if (allocated(bmp, i))
				print_inode(fs, i);
	}
	free(bmp);
}

static void
populate_fs(filesystem *fs, char **dopt, int didx, int squash_uids, int squash_perms, uint32_t fs_timestamp)
{
	int i;
	for(i = 0; i < didx; i++)
	{
		struct stat st;
		FILE *fh;
		int pdir;
		char *pdest;
		ext2_ino_t nod = EXT2_ROOT_INO;
		if(fs)
			if((pdest = strchr(dopt[i], ':')))
			{
				*(pdest++) = 0;
				if(!(nod = find_path(fs, EXT2_ROOT_INO, pdest)))
					error_msg_and_die("path %s not found in filesystem", pdest);
			}
		stat(dopt[i], &st);
		switch(st.st_mode & S_IFMT)
		{
			case S_IFREG:
				fh = xfopen(dopt[i], "rb");
				add2fs_from_file(fs, nod, fh, fs_timestamp);
				fclose(fh);
				break;
			case S_IFDIR:
				if((pdir = open(".", O_RDONLY)) < 0)
					perror_msg_and_die(".");
				if(chdir(dopt[i]) < 0)
					perror_msg_and_die(dopt[i]);
				add2fs_from_dir(fs, nod, squash_uids, squash_perms, fs_timestamp);
				if(fchdir(pdir) < 0)
					perror_msg_and_die("fchdir");
				if(close(pdir) < 0)
					perror_msg_and_die("close");
				break;
			default:
				error_msg_and_die("%s is neither a file nor a directory", dopt[i]);
		}
	}
}

static void
showversion(void)
{
	printf("genext2fs " VERSION "\n");
}

static void
showhelp(void)
{
	fprintf(stderr, "Usage: %s [options] image\n"
	"Create an ext2 filesystem image from directories/files\n\n"
	"  -d, --root <directory>\n"
	"  -D, --devtable <file>\n"
	"  -g, --block-map <path>     Generate a block map file for this path.\n"
	"  -e, --fill-value <value>   Fill unallocated blocks with value.\n"
	"  -f, --faketime             Set filesystem timestamps to 0 (for testing).\n"
	"  -q, --squash               Same as \"-U -P\".\n"
	"  -U, --squash-uids          Squash owners making all files be owned by root.\n"
	"  -P, --squash-perms         Squash permissions on all files.\n"
	"  -h, --help\n"
	"  -V, --version\n"
	"  -v, --verbose\n\n"
	"Report bugs to genext2fs-devel@lists.sourceforge.net\n", app_name);
}

#define MAX_DOPT 128
#define MAX_GOPT 128

#define MAX_FILENAME 255

extern char* optarg;
extern int optind, opterr, optopt;

int
main(int argc, char **argv)
{
	int nbresrvd = -1;
	int fs_timestamp = -1;
	char * fsout = "-";
	char * dopt[MAX_DOPT];
	int didx = 0;
	char * gopt[MAX_GOPT];
	int gidx = 0;
	int verbose = 0;
	int emptyval = -1;
	int squash_uids = 0;
	int squash_perms = 0;
	ext2_filsys fs;
	int i;
	int c;

#if HAVE_GETOPT_LONG
	struct option longopts[] = {
	  { "root",		required_argument,	NULL, 'd' },
	  { "devtable",		required_argument,	NULL, 'D' },
	  { "block-map",	required_argument,	NULL, 'g' },
	  { "fill-value",	required_argument,	NULL, 'e' },
	  { "faketime",		no_argument,		NULL, 'f' },
	  { "squash",		no_argument,		NULL, 'q' },
	  { "squash-uids",	no_argument,		NULL, 'U' },
	  { "squash-perms",	no_argument,		NULL, 'P' },
	  { "help",		no_argument,		NULL, 'h' },
	  { "version",		no_argument,		NULL, 'V' },
	  { "verbose",		no_argument,		NULL, 'v' },
	  { 0, 0, 0, 0}
	} ;

	app_name = argv[0];

	while((c = getopt_long(argc, argv, "d:D:N:g:e:fqUPhVv", longopts, NULL)) != EOF) {
#else
	app_name = argv[0];

	while((c = getopt(argc, argv,      "d:D:N:g:e:fqUPhVv")) != EOF) {
#endif /* HAVE_GETOPT_LONG */
		switch(c)
		{
			case 'd':
			case 'D':
				if (didx >= MAX_DOPT)
					error_msg_and_die("too much -d options");
				dopt[didx++] = optarg;
				break;
			case 'g':
				if (didx >= MAX_GOPT)
					error_msg_and_die("too much -g options");
				gopt[gidx++] = optarg;
				break;
			case 'e':
				emptyval = atoi(optarg);
				break;
			case 'f':
				fs_timestamp = 0;
				break;
			case 'q':
				squash_uids = 1;
				squash_perms = 1;
				break;
			case 'U':
				squash_uids = 1;
				break;
			case 'P':
				squash_perms = 1;
				break;
			case 'h':
				showhelp();
				exit(0);
			case 'V':
				showversion();
				exit(0);
			case 'v':
				verbose = 1;
				showversion();
				break;
			default:
				error_msg_and_die("Note: options have changed, see --help or the man page.");
		}
	}

	if(optind < (argc - 1))
		error_msg_and_die("Too many arguments. Try --help or else see the man page.");
	if(optind > (argc - 1))
		error_msg_and_die("Not enough arguments. Try --help or else see the man page.");
	fsout = argv[optind];

	hdlinks.hdl = (struct hdlink_s *)xrealloc(NULL, hdlink_cnt * sizeof(struct hdlink_s));
	hdlinks.count = 0 ;

	if (!strcmp(fsout, "-"))
		error_msg_and_die("output to stream not supported");

	fs = load_fs(fsout, 0);

	populate_fs(fs, dopt, didx, squash_uids, squash_perms, fs_timestamp);

	/* TODO clear unused space */
#if 0
	if (emptyval >= 0) {
		blk64_t b, count = ext2fs_blocks_count(fs->super);
		for(b = 0; b < count; b++) {
			blk64_t prev = b;
			if (!ext2fs_get_free_blocks2(fs, b, 0, 1, 0, &b) || b < prev)
				break;
			memset(get_blk(fs, b), emptyval, fs->blocksize);
		}
	}
#endif

	if(verbose)
		print_fs(fs);

	for(i = 0; i < gidx; i++)
	{
		ext2_ino_t nod;
		struct ext2_inode inode;
		char fname[MAX_FILENAME];
		char *p;
		FILE *fh;
		if(!(nod = find_path(fs, EXT2_ROOT_INO, gopt[i])))
			error_msg_and_die("path %s not found in filesystem", gopt[i]);
		while((p = strchr(gopt[i], '/')))
			*p = '_';
		SNPRINTF(fname, MAX_FILENAME-1, "%s.blk", gopt[i]);
		fh = xfopen(fname, "wb");
		fprintf(fh, "%lld:", (long long) EXT2_I_SIZE(get_nod(fs, nod, &inode)));
		flist_blocks(fs, nod, fh);
		fclose(fh);
	}

	free_fs(fs);
	return 0;
}
