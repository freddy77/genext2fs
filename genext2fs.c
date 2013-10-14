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

struct stats {
	unsigned long nblocks;
	unsigned long ninodes;
};

// block size

#define BLOCKSIZE         1024
#define BLOCKS_PER_GROUP  8192
#define INODES_PER_GROUP  8192
/* Percentage of blocks that are reserved.*/
#define RESERVED_BLOCKS       5/100
#define MAX_RESERVED_BLOCKS  25/100


// inode block size (why is it != BLOCKSIZE ?!?)
/* The field i_blocks in the ext2 inode stores the number of data blocks
   but in terms of 512 bytes. That is what INODE_BLOCKSIZE represents.
   INOBLK is the number of such blocks in an actual disk block            */

#define INODE_BLOCKSIZE   512
#define INOBLK            (BLOCKSIZE / INODE_BLOCKSIZE)

// reserved inodes

#define EXT2_BAD_INO         1     // Bad blocks inode
#define EXT2_ROOT_INO        2     // Root inode
#define EXT2_ACL_IDX_INO     3     // ACL inode
#define EXT2_ACL_DATA_INO    4     // ACL inode
#define EXT2_BOOT_LOADER_INO 5     // Boot loader inode
#define EXT2_UNDEL_DIR_INO   6     // Undelete directory inode
//#define EXT2_FIRST_INO       11    // First non reserved inode

// magic number for ext2

#define EXT2_MAGIC_NUMBER  0xEF53


// direct/indirect block addresses

// TODO this is different from ext2lib (actually not used)
//#define EXT2_NDIR_BLOCKS   11                    // direct blocks
#define EXT2_INIT_BLOCK    0xFFFFFFFF            // just initialized (not really a block address)

// end of a block walk

#define WALK_END           0xFFFFFFFE

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

// options

#define OP_HOLES     0x01       // make files with holes

/* Defines for accessing group details */

// Number of groups in the filesystem
#define GRP_NBGROUPS(fs) \
	(((fs)->sb.s_blocks_count - fs->sb.s_first_data_block + \
	  (fs)->sb.s_blocks_per_group - 1) / (fs)->sb.s_blocks_per_group)

// Get group block bitmap (bbm) given the group number
//#define GRP_GET_GROUP_BBM(fs,grp) ( get_blk((fs),(fs)->gd[(grp)].bg_block_bitmap) )

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

// Given a block number find the group it belongs to
#define GRP_GROUP_OF_BLOCK(fs,blk) ( ((blk)-1) / (fs)->sb.s_blocks_per_group)
	
//Given a block number get the block bitmap that covers it
#define GRP_GET_BLOCK_BITMAP(fs,blk) \
	( GRP_GET_GROUP_BBM((fs),GRP_GROUP_OF_BLOCK((fs),(blk))) )

//Given a block number find its offset within the block bitmap that covers it
#define GRP_BBM_OFFSET(fs,blk) \
	( (blk) - GRP_GROUP_OF_BLOCK((fs),(blk))*(fs)->sb.s_blocks_per_group )


// used types

typedef signed char int8;
typedef unsigned char uint8;
typedef signed short int16;
typedef unsigned short uint16;
typedef signed int int32;
typedef unsigned int uint32;


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

// endianness swap


// on-disk structures
// this trick makes me declare things only once
// (once for the structures, once for the endianness swap)

#define superblock_decl \
	udecl32(s_inodes_count)        /* Count of inodes in the filesystem */ \
	udecl32(s_blocks_count)        /* Count of blocks in the filesystem */ \
	udecl32(s_r_blocks_count)      /* Count of the number of reserved blocks */ \
	udecl32(s_free_blocks_count)   /* Count of the number of free blocks */ \
	udecl32(s_free_inodes_count)   /* Count of the number of free inodes */ \
	udecl32(s_first_data_block)    /* The first block which contains data */ \
	udecl32(s_log_block_size)      /* Indicator of the block size */ \
	decl32(s_log_frag_size)        /* Indicator of the size of the fragments */ \
	udecl32(s_blocks_per_group)    /* Count of the number of blocks in each block group */ \
	udecl32(s_frags_per_group)     /* Count of the number of fragments in each block group */ \
	udecl32(s_inodes_per_group)    /* Count of the number of inodes in each block group */ \
	udecl32(s_mtime)               /* The time that the filesystem was last mounted */ \
	udecl32(s_wtime)               /* The time that the filesystem was last written to */ \
	udecl16(s_mnt_count)           /* The number of times the file system has been mounted */ \
	decl16(s_max_mnt_count)        /* The number of times the file system can be mounted */ \
	udecl16(s_magic)               /* Magic number indicating ex2fs */ \
	udecl16(s_state)               /* Flags indicating the current state of the filesystem */ \
	udecl16(s_errors)              /* Flags indicating the procedures for error reporting */ \
	udecl16(s_minor_rev_level)     /* The minor revision level of the filesystem */ \
	udecl32(s_lastcheck)           /* The time that the filesystem was last checked */ \
	udecl32(s_checkinterval)       /* The maximum time permissable between checks */ \
	udecl32(s_creator_os)          /* Indicator of which OS created the filesystem */ \
	udecl32(s_rev_level)           /* The revision level of the filesystem */ \
	udecl16(s_def_resuid)          /* The default uid for reserved blocks */ \
	udecl16(s_def_resgid)          /* The default gid for reserved blocks */

#define groupdescriptor_decl \
	udecl32(bg_block_bitmap)       /* Block number of the block bitmap */ \
	udecl32(bg_inode_bitmap)       /* Block number of the inode bitmap */ \
	udecl32(bg_inode_table)        /* Block number of the inode table */ \
	udecl16(bg_free_blocks_count)  /* Free blocks in the group */ \
	udecl16(bg_free_inodes_count)  /* Free inodes in the group */ \
	udecl16(bg_used_dirs_count)    /* Number of directories in the group */ \
	udecl16(bg_pad)

#define inode_decl \
	udecl16(i_mode)                /* Entry type and file mode */ \
	udecl16(i_uid)                 /* User id */ \
	udecl32(i_size)                /* File/dir size in bytes */ \
	udecl32(i_atime)               /* Last access time */ \
	udecl32(i_ctime)               /* Creation time */ \
	udecl32(i_mtime)               /* Last modification time */ \
	udecl32(i_dtime)               /* Deletion time */ \
	udecl16(i_gid)                 /* Group id */ \
	udecl16(i_links_count)         /* Number of (hard) links to this inode */ \
	udecl32(i_blocks)              /* Number of blocks used (1 block = 512 bytes) */ \
	udecl32(i_flags)               /* ??? */ \
	udecl32(i_reserved1) \
	utdecl32(i_block,15)           /* Blocks table */ \
	udecl32(i_version)             /* ??? */ \
	udecl32(i_file_acl)            /* File access control list */ \
	udecl32(i_dir_acl)             /* Directory access control list */ \
	udecl32(i_faddr)               /* Fragment address */ \
	udecl8(i_frag)                 /* Fragments count*/ \
	udecl8(i_fsize)                /* Fragment size */ \
	udecl16(i_pad1)

#define directory_decl \
	udecl32(d_inode)               /* Inode entry */ \
	udecl16(d_rec_len)             /* Total size on record */ \
	udecl16(d_name_len)            /* Size of entry name */

#define decl8(x) int8 x;
#define udecl8(x) uint8 x;
#define decl16(x) int16 x;
#define udecl16(x) uint16 x;
#define decl32(x) int32 x;
#define udecl32(x) uint32 x;
#define utdecl32(x,n) uint32 x[n];

#if 0
typedef struct
{
	superblock_decl
	uint32 s_reserved[235];       // Reserved
} superblock;

typedef struct
{
	groupdescriptor_decl
	uint32 bg_reserved[3];
} groupdescriptor;

typedef struct
{
	inode_decl
	uint32 i_reserved2[2];
} inode;

typedef struct
{
	directory_decl
	char d_name[0];
} directory;

typedef uint8 block[BLOCKSIZE];
#endif

/* blockwalker fields:
   The blockwalker is used to access all the blocks of a file (including
   the indirection blocks) through repeated calls to walk_bw.  
   
   bpdir -> index into the inode->i_block[]. Indicates level of indirection.
   bnum -> total number of blocks so far accessed. including indirection
           blocks.
   bpind,bpdind,bptind -> index into indirection blocks.
   
   bpind, bpdind, bptind do *NOT* index into single, double and triple
   indirect blocks resp. as you might expect from their names. Instead 
   they are in order the 1st, 2nd & 3rd index to be used
   
   As an example..
   To access data block number 70000:
        bpdir: 15 (we are doing triple indirection)
        bpind: 0 ( index into the triple indirection block)
        bpdind: 16 ( index into the double indirection block)
        bptind: 99 ( index into the single indirection block)
	70000 = 12 + 256 + 256*256 + 16*256 + 100 (indexing starts from zero)

   So,for double indirection bpind will index into the double indirection 
   block and bpdind into the single indirection block. For single indirection
   only bpind will be used.
*/
   
typedef struct
{
	uint32 bnum;
	uint32 bpdir;
	uint32 bpind;
	uint32 bpdind;
	uint32 bptind;
} blockwalker;


/* Filesystem structure that support groups */
#if BLOCKSIZE == 1024
#if 0
typedef struct
{
	block zero;            // The famous block 0
	superblock sb;         // The superblock
	groupdescriptor gd[0]; // The group descriptors
} filesystem;
#endif
typedef struct struct_ext2_filsys filesystem;
#else
#error UNHANDLED BLOCKSIZE
#endif

// now the endianness swap

#undef decl8
#undef udecl8
#undef decl16
#undef udecl16
#undef decl32
#undef udecl32

#define HDLINK_CNT   16
static int32 hdlink_cnt = HDLINK_CNT;
struct hdlink_s
{
	uint32	src_inode;
	uint32	dst_nod;
};

struct hdlinks_s 
{
	int32 count;
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

int
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

#if 0
// temporary working block
static inline uint8 *
get_workblk(void)
{
	unsigned char* b=calloc(1,BLOCKSIZE);
	return b;
}
#endif

#if 0
static inline void
free_workblk(block b)
{
	free(b);
}
#endif

/* Rounds qty upto a multiple of siz. siz should be a power of 2 */
static inline uint32
rndup(uint32 qty, uint32 siz)
{
	return (qty + (siz - 1)) & ~(siz - 1);
}

#if 0
// check if something is allocated in the bitmap
static inline uint32
allocated(block b, uint32 item)
{
	return b[(item-1) / 8] & (1 << ((item-1) % 8));
}
#endif

#if 0
// return a given block from a filesystem
static inline uint8 *
get_blk(filesystem *fs, uint32 blk)
{
	return (uint8*)fs + blk*BLOCKSIZE;
}
#endif

#if 0
// return a given inode from a filesystem
static inline inode *
get_nod(filesystem *fs, uint32 nod)
{
	int grp,offset;
	inode *itab;

	offset = GRP_IBM_OFFSET(fs,nod);
	grp = GRP_GROUP_OF_INODE(fs,nod);
	itab = (inode *)get_blk(fs, fs->gd[grp].bg_inode_table);
	return itab+offset-1;
}
#endif

#if 0
// allocate a given block/inode in the bitmap
// allocate first free if item == 0
static uint32
allocate(block b, uint32 item)
{
	if(!item)
	{
		int i;
		uint8 bits;
		for(i = 0; i < BLOCKSIZE; i++)
			if((bits = b[i]) != (uint8)-1)
			{
				int j;
				for(j = 0; j < 8; j++)
					if(!(bits & (1 << j)))
						break;
				item = i * 8 + j + 1;
				break;
			}
		if(i == BLOCKSIZE)
			return 0;
	}
	b[(item-1) / 8] |= (1 << ((item-1) % 8));
	return item;
}
#endif

#if 0
// deallocate a given block/inode
static void
deallocate(block b, uint32 item)
{
	b[(item-1) / 8] &= ~(1 << ((item-1) % 8));
}
#endif

#if 0
// allocate a block
static uint32
alloc_blk(filesystem *fs, uint32 nod)
{
	uint32 bk=0;
	uint32 grp,nbgroups;

	grp = GRP_GROUP_OF_INODE(fs,nod);
	nbgroups = GRP_NBGROUPS(fs);
	if(!(bk = allocate(get_blk(fs,fs->gd[grp].bg_block_bitmap), 0))) {
		for(grp=0;grp<nbgroups && !bk;grp++)
			bk=allocate(get_blk(fs,fs->gd[grp].bg_block_bitmap),0);
		grp--;
	}
	if (!bk)
		error_msg_and_die("couldn't allocate a block (no free space)");
	if(!(fs->gd[grp].bg_free_blocks_count--))
		error_msg_and_die("group descr %d. free blocks count == 0 (corrupted fs?)",grp);
	if(!(fs->sb.s_free_blocks_count--))
		error_msg_and_die("superblock free blocks count == 0 (corrupted fs?)");
	return fs->sb.s_blocks_per_group*grp + bk;
}
#endif

#if 0
// free a block
static void
free_blk(filesystem *fs, uint32 bk)
{
	uint32 grp;

	grp = bk / fs->sb.s_blocks_per_group;
	bk %= fs->sb.s_blocks_per_group;
	deallocate(get_blk(fs,fs->gd[grp].bg_block_bitmap), bk);
	fs->gd[grp].bg_free_blocks_count++;
	fs->sb.s_free_blocks_count++;
}
#endif

#if 0
// allocate an inode
static uint32
alloc_nod(filesystem *fs)
{
	ext2_ino_t nod;
	uint32 best_group=0;
	uint32 grp,nbgroups,avefreei;

	nbgroups = GRP_NBGROUPS(fs);

	/* Distribute inodes amongst all the blocks                           */
	/* For every block group with more than average number of free inodes */
	/* find the one with the most free blocks and allocate node there     */
	/* Idea from find_group_dir in fs/ext2/ialloc.c in 2.4.19 kernel      */
	/* We do it for all inodes.                                           */
	avefreei  =  fs->sb.s_free_inodes_count / nbgroups;
	for(grp=0; grp<nbgroups; grp++) {
		if (fs->gd[grp].bg_free_inodes_count < avefreei ||
		    fs->gd[grp].bg_free_inodes_count == 0)
			continue;
		if (!best_group || 
			fs->gd[grp].bg_free_blocks_count > fs->gd[best_group].bg_free_blocks_count)
			best_group = grp;
	}
	if (!(nod = allocate(get_blk(fs,fs->gd[best_group].bg_inode_bitmap),0)))
		error_msg_and_die("couldn't allocate an inode (no free inode)");
	if(!(fs->gd[best_group].bg_free_inodes_count--))
		error_msg_and_die("group descr. free blocks count == 0 (corrupted fs?)");
	if(!(fs->sb.s_free_inodes_count--))
		error_msg_and_die("superblock free blocks count == 0 (corrupted fs?)");
	return fs->sb.s_inodes_per_group*best_group+nod;
}
#endif

#if 0
// print a bitmap allocation
static void
print_bm(block b, uint32 max)
{
	uint32 i;
	printf("----+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0\n");
	for(i=1; i <= max; i++)
	{
		putchar(allocated(b, i) ? '*' : '.');
		if(!(i % 100))
			printf("\n");
	}
	if((i-1) % 100)
		printf("\n");
}
#endif

#if 0
// initalize a blockwalker (iterator for blocks list)
static inline void
init_bw(blockwalker *bw)
{
	bw->bnum = 0;
	bw->bpdir = EXT2_INIT_BLOCK;
}
#endif

#if 0
// return next block of inode (WALK_END for end)
// if *create>0, append a newly allocated block at the end
// if *create<0, free the block - warning, the metadata blocks contents is
//				  used after being freed, so once you start
//				  freeing blocks don't stop until the end of
//				  the file. moreover, i_blocks isn't updated.
//				  in fact, don't do that, just use extend_blk
// if hole!=0, create a hole in the file
static uint32
walk_bw(filesystem *fs, ext2_ino_t nod, blockwalker *bw, int32 *create, uint32 hole)
{
	uint32 *bkref = 0;
	uint32 *b;
	int extend = 0, reduce = 0;
	if(create && (*create) < 0)
		reduce = 1;
	if(bw->bnum >= get_nod(fs, nod)->i_blocks / INOBLK)
	{
		if(create && (*create) > 0)
		{
			(*create)--;
			extend = 1;
		}
		else	
			return WALK_END;
	}
	// first direct block
	if(bw->bpdir == EXT2_INIT_BLOCK)
	{
		bkref = &get_nod(fs, nod)->i_block[bw->bpdir = 0];
		if(extend) // allocate first block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free first block
			free_blk(fs, *bkref);
	}
	// direct block
	else if(bw->bpdir < EXT2_NDIR_BLOCKS)
	{
		bkref = &get_nod(fs, nod)->i_block[++bw->bpdir];
		if(extend) // allocate block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free block
			free_blk(fs, *bkref);
	}
	// first block in indirect block
	else if(bw->bpdir == EXT2_NDIR_BLOCKS)
	{
		bw->bnum++;
		bw->bpdir = EXT2_IND_BLOCK;
		bw->bpind = 0;
		if(extend) // allocate indirect block
			get_nod(fs, nod)->i_block[bw->bpdir] = alloc_blk(fs,nod);
		if(reduce) // free indirect block
			free_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		bkref = &b[bw->bpind];
		if(extend) // allocate first block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free first block
			free_blk(fs, *bkref);
	}
	// block in indirect block
	else if((bw->bpdir == EXT2_IND_BLOCK) && (bw->bpind < BLOCKSIZE/4 - 1))
	{
		bw->bpind++;
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		bkref = &b[bw->bpind];
		if(extend) // allocate block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free block
			free_blk(fs, *bkref);
	}
	// first block in first indirect block in first double indirect block
	else if(bw->bpdir == EXT2_IND_BLOCK)
	{
		bw->bnum += 2;
		bw->bpdir = EXT2_DIND_BLOCK;
		bw->bpind = 0;
		bw->bpdind = 0;
		if(extend) // allocate double indirect block
			get_nod(fs, nod)->i_block[bw->bpdir] = alloc_blk(fs,nod);
		if(reduce) // free double indirect block
			free_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		if(extend) // allocate first indirect block
			b[bw->bpind] = alloc_blk(fs,nod);
		if(reduce) // free  firstindirect block
			free_blk(fs, b[bw->bpind]);
		b = (uint32*)get_blk(fs, b[bw->bpind]);
		bkref = &b[bw->bpdind];
		if(extend) // allocate first block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free first block
			free_blk(fs, *bkref);
	}
	// block in indirect block in double indirect block
	else if((bw->bpdir == EXT2_DIND_BLOCK) && (bw->bpdind < BLOCKSIZE/4 - 1))
	{
		bw->bpdind++;
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		b = (uint32*)get_blk(fs, b[bw->bpind]);
		bkref = &b[bw->bpdind];
		if(extend) // allocate block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free block
			free_blk(fs, *bkref);
	}
	// first block in indirect block in double indirect block
	else if((bw->bpdir == EXT2_DIND_BLOCK) && (bw->bpind < BLOCKSIZE/4 - 1))
	{
		bw->bnum++;
		bw->bpdind = 0;
		bw->bpind++;
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		if(extend) // allocate indirect block
			b[bw->bpind] = alloc_blk(fs,nod);
		if(reduce) // free indirect block
			free_blk(fs, b[bw->bpind]);
		b = (uint32*)get_blk(fs, b[bw->bpind]);
		bkref = &b[bw->bpdind];
		if(extend) // allocate first block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free first block
			free_blk(fs, *bkref);
	}

	/* Adding support for triple indirection */
	/* Just starting triple indirection. Allocate the indirection
	   blocks and the first data block
	 */
	else if (bw->bpdir == EXT2_DIND_BLOCK) 
	{
	  	bw->bnum += 3;
		bw->bpdir = EXT2_TIND_BLOCK;
		bw->bpind = 0;
		bw->bpdind = 0;
		bw->bptind = 0;
		if(extend) // allocate triple indirect block
			get_nod(fs, nod)->i_block[bw->bpdir] = alloc_blk(fs,nod);
		if(reduce) // free triple indirect block
			free_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		if(extend) // allocate first double indirect block
			b[bw->bpind] = alloc_blk(fs,nod);
		if(reduce) // free first double indirect block
			free_blk(fs, b[bw->bpind]);
		b = (uint32*)get_blk(fs, b[bw->bpind]);
		if(extend) // allocate first indirect block
			b[bw->bpdind] = alloc_blk(fs,nod);
		if(reduce) // free first indirect block
			free_blk(fs, b[bw->bpind]);
		b = (uint32*)get_blk(fs, b[bw->bpdind]);
		bkref = &b[bw->bptind];
		if(extend) // allocate first data block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free first block
			free_blk(fs, *bkref);
	}
	/* Still processing a single indirect block down the indirection
	   chain.Allocate a data block for it
	 */
	else if ( (bw->bpdir == EXT2_TIND_BLOCK) && 
		  (bw->bptind < BLOCKSIZE/4 -1) )
	{
		bw->bptind++;
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		b = (uint32*)get_blk(fs, b[bw->bpind]);
		b = (uint32*)get_blk(fs, b[bw->bpdind]);
		bkref = &b[bw->bptind];
		if(extend) // allocate data block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free block
			free_blk(fs, *bkref);
	}
	/* Finished processing a single indirect block. But still in the 
	   same double indirect block. Allocate new single indirect block
	   for it and a data block
	 */
	else if ( (bw->bpdir == EXT2_TIND_BLOCK) &&
		  (bw->bpdind < BLOCKSIZE/4 -1) )
	{
		bw->bnum++;
		bw->bptind = 0;
		bw->bpdind++;
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		b = (uint32*)get_blk(fs, b[bw->bpind]);
		if(extend) // allocate single indirect block
			b[bw->bpdind] = alloc_blk(fs,nod);
		if(reduce) // free indirect block
			free_blk(fs, b[bw->bpind]);
		b = (uint32*)get_blk(fs, b[bw->bpdind]);
		bkref = &b[bw->bptind];
		if(extend) // allocate first data block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free first block
			free_blk(fs, *bkref);
	}
	/* Finished processing a double indirect block. Allocate the next
	   double indirect block and the single,data blocks for it
	 */
	else if ( (bw->bpdir == EXT2_TIND_BLOCK) && 
		  (bw->bpind < BLOCKSIZE/4 - 1) )
	{
		bw->bnum += 2;
		bw->bpdind = 0;
		bw->bptind = 0;
		bw->bpind++;
		b = (uint32*)get_blk(fs, get_nod(fs, nod)->i_block[bw->bpdir]);
		if(extend) // allocate double indirect block
			b[bw->bpind] = alloc_blk(fs,nod);
		if(reduce) // free double indirect block
			free_blk(fs, b[bw->bpind]);
		b = (uint32*)get_blk(fs, b[bw->bpind]);
		if(extend) // allocate single indirect block
			b[bw->bpdind] = alloc_blk(fs,nod);
		if(reduce) // free indirect block
			free_blk(fs, b[bw->bpind]);
		b = (uint32*)get_blk(fs, b[bw->bpdind]);
		bkref = &b[bw->bptind];
		if(extend) // allocate first block
			*bkref = hole ? 0 : alloc_blk(fs,nod);
		if(reduce) // free first block
			free_blk(fs, *bkref);
	}
	else
		error_msg_and_die("file too big !"); 
	/* End change for walking triple indirection */

	if(*bkref)
	{
		bw->bnum++;
		if(!reduce && !allocated(GRP_GET_BLOCK_BITMAP(fs,*bkref), GRP_BBM_OFFSET(fs,*bkref)))
			error_msg_and_die("[block %d of inode %d is unallocated !]", *bkref, nod);
	}
	if(extend)
		get_nod(fs, nod)->i_blocks = bw->bnum * INOBLK;
	return *bkref;
}
#endif

#if 0
// add blocks to an inode (file/dir/etc...)
static void
extend_blk(filesystem *fs, ext2_ino_t nod, block b, int amount)
{
	int create = amount;
	blockwalker bw, lbw;
	uint32 bk;
	init_bw(&bw);
	if(amount < 0)
	{
		uint32 i;
		for(i = 0; i < get_nod(fs, nod)->i_blocks / INOBLK + amount; i++)
			walk_bw(fs, nod, &bw, 0, 0);
		while(walk_bw(fs, nod, &bw, &create, 0) != WALK_END)
			/*nop*/;
		get_nod(fs, nod)->i_blocks += amount * INOBLK;
	}
	else
	{
		lbw = bw;
		while((bk = walk_bw(fs, nod, &bw, 0, 0)) != WALK_END)
			lbw = bw;
		bw = lbw;
		while(create)
		{
			int i, copyb = 0;
			if(!(fs->sb.s_reserved[200] & OP_HOLES))
				copyb = 1;
			else
				for(i = 0; i < BLOCKSIZE / 4; i++)
					if(((int32*)(b + BLOCKSIZE * (amount - create)))[i])
					{
						copyb = 1;
						break;
					}
			if((bk = walk_bw(fs, nod, &bw, &create, !copyb)) == WALK_END)
				break;
			if(copyb)
				memcpy(get_blk(fs, bk), b + BLOCKSIZE * (amount - create - 1), BLOCKSIZE);
		}
	}
}
#endif

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
	errcode_t rc;
	ext2_ino_t ino;

	// TODO not sure about root/cwd parameters
//	rc = ext2fs_namei(e2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, path, ino);
	if (name[0] == '/')
		nod = EXT2_ROOT_INO;
	rc = ext2fs_namei(fs, nod, nod, name, &ino);
	return rc ? 0 : ino;
}

static int do_modetoext2lag (mode_t mode)
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

static inline int old_valid_dev(dev_t dev)
{
	return major(dev) < 256 && minor(dev) < 256;
}

static inline __u16 old_encode_dev(dev_t dev)
{
	return (major(dev) << 8) | minor(dev);
}

static inline __u32 new_encode_dev(dev_t dev)
{
	unsigned major = major(dev);
	unsigned minor = minor(dev);
	return (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
}

ext2_ino_t do_create (ext2_filsys e2fs, 
	const ext2_ino_t ino, /** parent inode */
	const char *name,
	mode_t mode,
	dev_t dev, // TODO use it !! (how ??)
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
mknod_fs(filesystem *fs, ext2_ino_t parent_nod, const char *name, uint16 mode, uid_t uid, gid_t gid, uint8 major, uint8 minor, uint32 ctime, uint32 mtime)
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
mkdir_fs(filesystem *fs, ext2_ino_t parent_nod, const char *name, uint32 mode,
	uid_t uid, gid_t gid, uint32 ctime, uint32 mtime)
{
	return mknod_fs(fs, parent_nod, name, mode|FM_IFDIR, uid, gid, 0, 0, ctime, mtime);
}

#define EXT2_FILE_SHARED_INODE 0x8000

ext2_file_t do_open (ext2_filsys e2fs, ext2_ino_t ino, int flags)
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
static size_t do_write (ext2_file_t efile, const char *buf, size_t size, off_t offset)
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
do_release (ext2_file_t efile)
{
	errcode_t rc;

	rc = ext2fs_file_close(efile);
	if (rc)
		return -EIO;

	return 0;
}


// make a symlink
static ext2_ino_t
mklink_fs(filesystem *e2fs, ext2_ino_t parent_nod, const char *destname, size_t sourcelen, uint8 *sourcename, uid_t uid, gid_t gid, uint32 ctime, uint32 mtime)
{
	int rt;
	size_t wr;
	ext2_file_t efile;
	ext2_ino_t ino;

	debugf("enter");
	debugf("source: %s, dest: %s", sourcename, destname);

	/* a short symlink is stored in the inode (recycling the i_block array) */
	if (sourcelen < (EXT2_N_BLOCKS * sizeof(__u32))) {
		ino = do_create(e2fs, parent_nod, destname, LINUX_S_IFLNK | 0777, 0, sourcename, uid, gid, ctime, mtime);
	} else {
		ino = do_create(e2fs, parent_nod, destname, LINUX_S_IFLNK | 0777, 0, NULL, uid, gid, ctime, mtime);

		efile = do_open(e2fs, ino, O_WRONLY);
		if (efile == NULL)
			perror_msg_and_die("do_open(%d); failed", ino);
		wr = do_write(efile, sourcename, sourcelen, 0);
		if (wr != strlen(sourcename))
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
mkfile_fs(filesystem *fs, ext2_ino_t parent_nod, const char *name, uint32 mode, size_t size, FILE *f, uid_t uid, gid_t gid, uint32 ctime, uint32 mtime)
{
	ext2_file_t efile;
	uint8 * b;
	ext2_ino_t ino = mknod_fs(fs, parent_nod, name, mode|FM_IFREG, uid, gid, 0, 0, ctime, mtime);

	// TODO use fallocate, handle sparse files
	if (size) {
		int rt;
		size_t wr;

		if(!(b = (uint8*)calloc(rndup(size, BLOCKSIZE), 1)))
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
static uint32
get_mode(struct stat *st)
{
	uint32 mode = 0;

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
add2fs_from_file(filesystem *fs, ext2_ino_t this_nod, FILE * fh, uint32 fs_timestamp, struct stats *stats)
{
	unsigned long mode, uid, gid, major, minor;
	unsigned long start, increment, count;
	ext2_ino_t nod;
	uint32 ctime, mtime;
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
		if(stats) {
			if(count > 0)
				stats->ninodes += count - start;
			else
				stats->ninodes++;
		} else {
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
add2fs_from_dir(filesystem *fs, ext2_ino_t this_nod, int squash_uids, int squash_perms, uint32 fs_timestamp, struct stats *stats)
{
	ext2_ino_t nod;
	uint32 uid, gid, mode, ctime, mtime;
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
		if(stats)
			switch(st.st_mode & S_IFMT)
			{
				case S_IFLNK:
				case S_IFREG:
					if((st.st_mode & S_IFMT) == S_IFREG || st.st_size > 4 * (EXT2_TIND_BLOCK+1))
						stats->nblocks += (st.st_size + BLOCKSIZE - 1) / BLOCKSIZE;
				case S_IFCHR:
				case S_IFBLK:
				case S_IFIFO:
				case S_IFSOCK:
					stats->ninodes++;
					break;
				case S_IFDIR:
					stats->ninodes++;
					if(chdir(dent->d_name) < 0)
						perror_msg_and_die(dent->d_name);
					add2fs_from_dir(fs, this_nod, squash_uids, squash_perms, fs_timestamp, stats);
					chdir("..");
					break;
				default:
					break;
			}
		else
		{
			save_nod = 0;
			/* Check for hardlinks */
			if (!S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && st.st_nlink > 1) {
				int32 hdlink = is_hardlink(st.st_ino);
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
					mklink_fs(fs, this_nod, name, st.st_size, (uint8*)lnk, uid, gid, ctime, mtime);
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
					add2fs_from_dir(fs, nod, squash_uids, squash_perms, fs_timestamp, stats);
					chdir("..");
					break;
				default:
					error_msg("ignoring entry %s", name);
			}
			if (save_nod) {
				if (hdlinks.count == hdlink_cnt) {
					if ((hdlinks.hdl = 
						 realloc (hdlinks.hdl, (hdlink_cnt + HDLINK_CNT) *
								  sizeof (struct hdlink_s))) == NULL) {
						error_msg_and_die("Not enough memory");
					}
					hdlink_cnt += HDLINK_CNT;
				}
				hdlinks.hdl[hdlinks.count].src_inode = st.st_ino;
				hdlinks.hdl[hdlinks.count].dst_nod = nod;
				hdlinks.count++;
			}
		}
	}
	closedir(dh);
}

#if 0
// initialize an empty filesystem
static filesystem *
init_fs(int nbblocks, int nbinodes, int nbresrvd, int holes, uint32 fs_timestamp)
{
	uint32 i;
	filesystem *fs;
	directory *d;
	uint8 * b;
	uint32 nod, first_block;
	uint32 nbgroups,nbinodes_per_group,overhead_per_group,free_blocks,
		free_blocks_per_group,nbblocks_per_group,min_nbgroups;
	uint32 gdsz,itblsz,bbmpos,ibmpos,itblpos;
	uint32 j;
	uint8 *bbm,*ibm;
	inode *itab0;
	
	if(nbresrvd < 0)
		error_msg_and_die("reserved blocks value is invalid. Note: options have changed, see --help or the man page.");
	if(nbinodes < EXT2_FIRST_INO - 1 + (nbresrvd ? 1 : 0))
		error_msg_and_die("too few inodes. Note: options have changed, see --help or the man page.");
	if(nbblocks < 8)
		error_msg_and_die("too few blocks. Note: options have changed, see --help or the man page.");

	/* nbinodes is the total number of inodes in the system.
	 * a block group can have no more than 8192 inodes.
	 */
	min_nbgroups = (nbinodes + INODES_PER_GROUP - 1) / INODES_PER_GROUP;

	/* nbblocks is the total number of blocks in the filesystem.
	 * a block group can have no more than 8192 blocks.
	 */
	first_block = (BLOCKSIZE == 1024);
	nbgroups = (nbblocks - first_block + BLOCKS_PER_GROUP - 1) / BLOCKS_PER_GROUP;
	if(nbgroups < min_nbgroups) nbgroups = min_nbgroups;
	nbblocks_per_group = rndup((nbblocks - first_block + nbgroups - 1)/nbgroups, 8);
	nbinodes_per_group = rndup((nbinodes + nbgroups - 1)/nbgroups,
						(BLOCKSIZE/sizeof(inode)));
	if (nbinodes_per_group < 16)
		nbinodes_per_group = 16; //minimum number b'cos the first 10 are reserved

	gdsz = rndup(nbgroups*sizeof(groupdescriptor),BLOCKSIZE)/BLOCKSIZE;
	itblsz = nbinodes_per_group * sizeof(inode)/BLOCKSIZE;
	overhead_per_group = 3 /*sb,bbm,ibm*/ + gdsz + itblsz;
	if((uint32)nbblocks - 1 < overhead_per_group * nbgroups)
		error_msg_and_die("too much overhead, try fewer inodes or more blocks. Note: options have changed, see --help or the man page.");
	free_blocks = nbblocks - overhead_per_group*nbgroups - 1 /*boot block*/;
	free_blocks_per_group = nbblocks_per_group - overhead_per_group;

	if(!(fs = (filesystem*)calloc(nbblocks, BLOCKSIZE)))
		error_msg_and_die("not enough memory for filesystem");

	// create the superblock for an empty filesystem
	fs->sb.s_inodes_count = nbinodes_per_group * nbgroups;
	fs->sb.s_blocks_count = nbblocks;
	fs->sb.s_r_blocks_count = nbresrvd;
	fs->sb.s_free_blocks_count = free_blocks;
	fs->sb.s_free_inodes_count = fs->sb.s_inodes_count - EXT2_FIRST_INO + 1;
	fs->sb.s_first_data_block = first_block;
	fs->sb.s_log_block_size = BLOCKSIZE >> 11;
	fs->sb.s_log_frag_size = BLOCKSIZE >> 11;
	fs->sb.s_blocks_per_group = nbblocks_per_group;
	fs->sb.s_frags_per_group = nbblocks_per_group;
	fs->sb.s_inodes_per_group = nbinodes_per_group;
	fs->sb.s_wtime = fs_timestamp;
	fs->sb.s_magic = EXT2_MAGIC_NUMBER;
	fs->sb.s_lastcheck = fs_timestamp;

	// set up groupdescriptors
	for(i=0, bbmpos=gdsz+2, ibmpos=bbmpos+1, itblpos=ibmpos+1;
		i<nbgroups;
		i++, bbmpos+=nbblocks_per_group, ibmpos+=nbblocks_per_group, itblpos+=nbblocks_per_group)
	{
		if(free_blocks > free_blocks_per_group) {
			fs->gd[i].bg_free_blocks_count = free_blocks_per_group;
			free_blocks -= free_blocks_per_group;
		} else {
			fs->gd[i].bg_free_blocks_count = free_blocks;
			free_blocks = 0; // this is the last block group
		}
		if(i)
			fs->gd[i].bg_free_inodes_count = nbinodes_per_group;
		else
			fs->gd[i].bg_free_inodes_count = nbinodes_per_group -
							EXT2_FIRST_INO + 2;
		fs->gd[i].bg_used_dirs_count = 0;
		fs->gd[i].bg_block_bitmap = bbmpos;
		fs->gd[i].bg_inode_bitmap = ibmpos;
		fs->gd[i].bg_inode_table = itblpos;
	}

	/* Mark non-filesystem blocks and inodes as allocated */
	/* Mark system blocks and inodes as allocated         */
	for(i = 0; i<nbgroups;i++) {

		/* Block bitmap */
		bbm = get_blk(fs,fs->gd[i].bg_block_bitmap);	
		//non-filesystem blocks
		for(j = fs->gd[i].bg_free_blocks_count
		        + overhead_per_group + 1; j <= BLOCKSIZE * 8; j++)
			allocate(bbm, j); 
		//system blocks
		for(j = 1; j <= overhead_per_group; j++)
			allocate(bbm, j); 
		
		/* Inode bitmap */
		ibm = get_blk(fs,fs->gd[i].bg_inode_bitmap);	
		//non-filesystem inodes
		for(j = fs->sb.s_inodes_per_group+1; j <= BLOCKSIZE * 8; j++)
			allocate(ibm, j);

		//system inodes
		if(i == 0)
			for(j = 1; j < EXT2_FIRST_INO; j++)
				allocate(ibm, j);
	}

	// make root inode and directory
	/* We have groups now. Add the root filesystem in group 0 */
	/* Also increment the directory count for group 0 */
	fs->gd[0].bg_free_inodes_count--;
	fs->gd[0].bg_used_dirs_count = 1;
	itab0 = (inode *)get_blk(fs,fs->gd[0].bg_inode_table);
	itab0[EXT2_ROOT_INO-1].i_mode = FM_IFDIR | FM_IRWXU | FM_IRGRP | FM_IROTH | FM_IXGRP | FM_IXOTH; 
	itab0[EXT2_ROOT_INO-1].i_ctime = fs_timestamp;
	itab0[EXT2_ROOT_INO-1].i_mtime = fs_timestamp;
	itab0[EXT2_ROOT_INO-1].i_atime = fs_timestamp;
	itab0[EXT2_ROOT_INO-1].i_size = BLOCKSIZE;
	itab0[EXT2_ROOT_INO-1].i_links_count = 2;

	if(!(b = get_workblk()))
		error_msg_and_die("get_workblk() failed.");
	d = (directory*)b;
	d->d_inode = EXT2_ROOT_INO;
	d->d_rec_len = sizeof(directory)+4;
	d->d_name_len = 1;
	strcpy(d->d_name, ".");
	d = (directory*)(b + d->d_rec_len);
	d->d_inode = EXT2_ROOT_INO;
	d->d_rec_len = BLOCKSIZE - (sizeof(directory)+4);
	d->d_name_len = 2;
	strcpy(d->d_name, "..");
	extend_blk(fs, EXT2_ROOT_INO, b, 1);

	// make lost+found directory and reserve blocks
	if(fs->sb.s_r_blocks_count)
	{
		nod = mkdir_fs(fs, EXT2_ROOT_INO, "lost+found", FM_IRWXU, 0, 0, fs_timestamp, fs_timestamp);
		memset(b, 0, BLOCKSIZE);
		((directory*)b)->d_rec_len = BLOCKSIZE;
		/* We run into problems with e2fsck if directory lost+found grows
		 * bigger than this. Need to find out why this happens - sundar
		 */
		if (fs->sb.s_r_blocks_count > fs->sb.s_blocks_count * MAX_RESERVED_BLOCKS ) 
			fs->sb.s_r_blocks_count = fs->sb.s_blocks_count * MAX_RESERVED_BLOCKS;
		for(i = 1; i < fs->sb.s_r_blocks_count; i++)
			extend_blk(fs, nod, b, 1);
		get_nod(fs, nod)->i_size = fs->sb.s_r_blocks_count * BLOCKSIZE;
	}
	free_workblk(b);

	// administrative info
	fs->sb.s_state = 1;
	fs->sb.s_max_mnt_count = 20;

	// options for me
	if(holes)
		fs->sb.s_reserved[200] |= OP_HOLES;
	
	return fs;
}
#endif

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

#if 0
// just walk through blocks list
static void
flist_blocks(filesystem *fs, ext2_ino_t nod, FILE *fh)
{
	blockwalker bw;
	uint32 bk;
	init_bw(&bw);
	while((bk = walk_bw(fs, nod, &bw, 0, 0)) != WALK_END)
		fprintf(fh, " %d", bk);
	fprintf(fh, "\n");
}
#endif

#if 0
// walk through blocks list
static void
list_blocks(filesystem *fs, ext2_ino_t nod)
{
	int bn = 0;
	blockwalker bw;
	uint32 bk;
	init_bw(&bw);
	printf("blocks in inode %d:", nod);
	while((bk = walk_bw(fs, nod, &bw, 0, 0)) != WALK_END)
		printf(" %d", bk), bn++;
	printf("\n%d blocks (%d bytes)\n", bn, bn * BLOCKSIZE);
}
endif

// saves blocks to FILE*
static void
write_blocks(filesystem *fs, ext2_ino_t nod, FILE* f)
{
	blockwalker bw;
	uint32 bk;
	int32 fsize = get_nod(fs, nod)->i_size;
	init_bw(&bw);
	while((bk = walk_bw(fs, nod, &bw, 0, 0)) != WALK_END)
	{
		if(fsize <= 0)
			error_msg_and_die("wrong size while saving inode %d", nod);
		if(fwrite(get_blk(fs, bk), (fsize > BLOCKSIZE) ? BLOCKSIZE : fsize, 1, f) != 1)
			error_msg_and_die("error while saving inode %d", nod);
		fsize -= BLOCKSIZE;
	}
}
#endif

#if 0
// print block/char device minor and major
static void
print_dev(filesystem *fs, ext2_ino_t nod)
{
	int minor, major;
	minor = ((uint8*)get_nod(fs, nod)->i_block)[0];
	major = ((uint8*)get_nod(fs, nod)->i_block)[1];
	printf("major: %d, minor: %d\n", major, minor);
}

// print an inode as a directory
static void
print_dir(filesystem *fs, ext2_ino_t nod)
{
	blockwalker bw;
	uint32 bk;
	init_bw(&bw);
	printf("directory for inode %d:\n", nod);
	while((bk = walk_bw(fs, nod, &bw, 0, 0)) != WALK_END)
	{
		directory *d;
		uint8 *b;
		b = get_blk(fs, bk);
		for(d = (directory*)b; (int8*)d + sizeof(*d) < (int8*)b + BLOCKSIZE; d = (directory*)((int8*)d + d->d_rec_len))
			if(d->d_inode)
			{
				int i;
				printf("entry '");
				for(i = 0; i < d->d_name_len; i++)
					putchar(d->d_name[i]);
				printf("' (inode %d): rec_len: %d (name_len: %d)\n", d->d_inode, d->d_rec_len, d->d_name_len);
			}
	}
}

// print a symbolic link
static void
print_link(filesystem *fs, ext2_ino_t nod)
{
	if(!get_nod(fs, nod)->i_blocks)
		printf("links to '%s'\n", (char*)get_nod(fs, nod)->i_block);
	else
	{
		printf("links to '");
		write_blocks(fs, nod, stdout);
		printf("'\n");
	}
}
#endif

// make a ls-like printout of permissions
static void
make_perms(uint32 mode, char perms[11])
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

#if 0
// print an inode
static void
print_inode(filesystem *fs, ext2_ino_t nod)
{
	char *s;
	char perms[11];
	if(!get_nod(fs, nod)->i_mode)
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
			s = (nod >= EXT2_FIRST_INO) ? "normal" : "unknown reserved"; 
	}
	printf("inode %d (%s, %d links): ", nod, s, get_nod(fs, nod)->i_links_count);
	if(!allocated(GRP_GET_INODE_BITMAP(fs,nod), GRP_IBM_OFFSET(fs,nod)))
	{
		printf("unallocated\n");
		return;
	}
	make_perms(get_nod(fs, nod)->i_mode, perms);
	printf("%s,  size: %d byte%s (%d block%s)\n", perms, plural(get_nod(fs, nod)->i_size), plural(get_nod(fs, nod)->i_blocks / INOBLK));
	switch(get_nod(fs, nod)->i_mode & FM_IFMT)
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
#endif

#if 0
// describes various fields in a filesystem
static void
print_fs(filesystem *fs)
{
	uint32 i;
	uint8 *ibm;

	printf("%d blocks (%d free, %d reserved), first data block: %d\n",
	       fs->sb.s_blocks_count, fs->sb.s_free_blocks_count,
	       fs->sb.s_r_blocks_count, fs->sb.s_first_data_block);
	printf("%d inodes (%d free)\n", fs->sb.s_inodes_count,
	       fs->sb.s_free_inodes_count);
	printf("block size = %d, frag size = %d\n",
	       fs->sb.s_log_block_size ? (fs->sb.s_log_block_size << 11) : 1024,
	       fs->sb.s_log_frag_size ? (fs->sb.s_log_frag_size << 11) : 1024);
	printf("number of groups: %d\n",GRP_NBGROUPS(fs));
	printf("%d blocks per group,%d frags per group,%d inodes per group\n",
	     fs->sb.s_blocks_per_group, fs->sb.s_frags_per_group,
	     fs->sb.s_inodes_per_group);
	printf("Size of inode table: %d blocks\n",
		(int)(fs->sb.s_inodes_per_group * sizeof(inode) / BLOCKSIZE));
	for (i = 0; i < GRP_NBGROUPS(fs); i++) {
		printf("Group No: %d\n", i+1);
		printf("block bitmap: block %d,inode bitmap: block %d, inode table: block %d\n",
		     fs->gd[i].bg_block_bitmap, fs->gd[i].bg_inode_bitmap,
		     fs->gd[i].bg_inode_table);
		printf("block bitmap allocation:\n");
		print_bm(GRP_GET_GROUP_BBM(fs, i),fs->sb.s_blocks_per_group);
		printf("inode bitmap allocation:\n");
		ibm = GRP_GET_GROUP_IBM(fs, i);
		print_bm(ibm, fs->sb.s_inodes_per_group);
		for (i = 1; i <= fs->sb.s_inodes_per_group; i++)
			if (allocated(ibm, i))
				print_inode(fs, i);
	}
}
#endif

#if 0
static void
dump_fs(filesystem *fs, FILE * fh)
{
	uint32 nbblocks = fs->sb.s_blocks_count;
	fs->sb.s_reserved[200] = 0;
	if(fwrite(fs, BLOCKSIZE, nbblocks, fh) < nbblocks)
		perror_msg_and_die("output filesystem image");
}
#endif

static void
populate_fs(filesystem *fs, char **dopt, int didx, int squash_uids, int squash_perms, uint32 fs_timestamp, struct stats *stats)
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
				add2fs_from_file(fs, nod, fh, fs_timestamp, stats);
				fclose(fh);
				break;
			case S_IFDIR:
				if((pdir = open(".", O_RDONLY)) < 0)
					perror_msg_and_die(".");
				if(chdir(dopt[i]) < 0)
					perror_msg_and_die(dopt[i]);
				add2fs_from_dir(fs, nod, squash_uids, squash_perms, fs_timestamp, stats);
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
	"  -x, --starting-image <image>\n"
	"  -d, --root <directory>\n"
	"  -D, --devtable <file>\n"
	"  -b, --size-in-blocks <blocks>\n"
	"  -i, --bytes-per-inode <bytes per inode>\n"
	"  -N, --number-of-inodes <number of inodes>\n"
	"  -m, --reserved-percentage <percentage of blocks to reserve>\n"
	"  -g, --block-map <path>     Generate a block map file for this path.\n"
	"  -e, --fill-value <value>   Fill unallocated blocks with value.\n"
	"  -z, --allow-holes          Allow files with holes.\n"
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
	int nbblocks = -1;
	int nbinodes = -1;
	int nbresrvd = -1;
	float bytes_per_inode = -1;
	float reserved_frac = -1;
	int fs_timestamp = -1;
	char * fsout = "-";
	char * fsin = 0;
	char * dopt[MAX_DOPT];
	int didx = 0;
	char * gopt[MAX_GOPT];
	int gidx = 0;
	int verbose = 0;
	int holes = 0;
	int emptyval = 0;
	int squash_uids = 0;
	int squash_perms = 0;
	uint16 endian = 1;
	int bigendian = !*(char*)&endian;
	ext2_filsys fs;
	int i;
	int c;
	struct stats stats;

#if HAVE_GETOPT_LONG
	struct option longopts[] = {
	  { "starting-image",	required_argument,	NULL, 'x' },
	  { "root",		required_argument,	NULL, 'd' },
	  { "devtable",		required_argument,	NULL, 'D' },
	  { "size-in-blocks",	required_argument,	NULL, 'b' },
	  { "bytes-per-inode",	required_argument,	NULL, 'i' },
	  { "number-of-inodes",	required_argument,	NULL, 'N' },
	  { "reserved-percentage", required_argument,	NULL, 'm' },
	  { "block-map",	required_argument,	NULL, 'g' },
	  { "fill-value",	required_argument,	NULL, 'e' },
	  { "allow-holes",	no_argument, 		NULL, 'z' },
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

	while((c = getopt_long(argc, argv, "x:d:D:b:i:N:m:g:e:zfqUPhVv", longopts, NULL)) != EOF) {
#else
	app_name = argv[0];

	while((c = getopt(argc, argv,      "x:d:D:b:i:N:m:g:e:zfqUPhVv")) != EOF) {
#endif /* HAVE_GETOPT_LONG */
		switch(c)
		{
			case 'x':
				fsin = optarg;
				break;
			case 'd':
			case 'D':
				dopt[didx++] = optarg;
				break;
			case 'b':
				nbblocks = SI_atof(optarg);
				break;
			case 'i':
				bytes_per_inode = SI_atof(optarg);
				break;
			case 'N':
				nbinodes = SI_atof(optarg);
				break;
			case 'm':
				reserved_frac = SI_atof(optarg) / 100;
				break;
			case 'g':
				gopt[gidx++] = optarg;
				break;
			case 'e':
				emptyval = atoi(optarg);
				break;
			case 'z':
				holes = 1;
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

	hdlinks.hdl = (struct hdlink_s *)malloc(hdlink_cnt * sizeof(struct hdlink_s));
	if (!hdlinks.hdl)
		error_msg_and_die("Not enough memory");
	hdlinks.count = 0 ;

	if(fsin)
	{
		/* TODO support "-" as standard input */
		fs = load_fs(fsin, 0);
	}
	else
	{
		/* TODO create filesystem */
		error_msg_and_die("TODO create filesystem");
	}
	
	populate_fs(fs, dopt, didx, squash_uids, squash_perms, fs_timestamp, NULL);

	/* TODO clear unused space */
#if 0
	if(emptyval) {
		uint32 b;
		for(b = 1; b < fs->sb.s_blocks_count; b++)
			if(!allocated(GRP_GET_BLOCK_BITMAP(fs,b),GRP_BBM_OFFSET(fs,b)))
				memset(get_blk(fs, b), emptyval, BLOCKSIZE);
	}
#endif

	// TODO print some information
#if 0
	if(verbose)
		print_fs(fs);
#endif

	// TODO support g option
#if 0
	for(i = 0; i < gidx; i++)
	{
		ext2_ino_t nod;
		char fname[MAX_FILENAME];
		char *p;
		FILE *fh;
		if(!(nod = find_path(fs, EXT2_ROOT_INO, gopt[i])))
			error_msg_and_die("path %s not found in filesystem", gopt[i]);
		while((p = strchr(gopt[i], '/')))
			*p = '_';
		SNPRINTF(fname, MAX_FILENAME-1, "%s.blk", gopt[i]);
		fh = xfopen(fname, "wb");
		fprintf(fh, "%d:", get_nod(fs, nod)->i_size);
		flist_blocks(fs, nod, fh);
		fclose(fh);
	}
#endif

	// TODO write to another file
#if 0
	if(strcmp(fsout, "-"))
	{
		FILE * fh = xfopen(fsout, "wb");
		dump_fs(fs, fh);
		fclose(fh);
	}
	else
		dump_fs(fs, stdout);
#endif
	free_fs(fs);
	return 0;
}
