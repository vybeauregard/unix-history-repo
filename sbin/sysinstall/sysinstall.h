/*
 * Copyright (c) 1994, Paul Richards.
 *
 * All rights reserved.
 *
 * This software may be used, modified, copied, distributed, and
 * sold, in both source and binary form provided that the above
 * copyright and these terms are retained, verbatim, as the first
 * lines of this file.  Under no circumstances is the author
 * responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with
 * its use.
 */

#define TITLE	"FreeBSD 2.0-ALPHA Installation"

#define BOOT1 "/stand/sdboot"
#define BOOT2 "/stand/bootsd"

#define MAX_NO_DEVICES 10
#define MAX_NO_DISKS	10
#define MAX_NO_MOUNTS 30
#define MAX_NO_FS	30
#define MAXFS	MAX_NO_FS


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ncurses.h>
#include <string.h>
#include <errno.h>
#include <dialog.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/dkbad.h>
#include <sys/disklabel.h>

#define min(a,b)        ((a) < (b) ? (a) : (b))
#define max(a,b)        ((a) > (b) ? (a) : (b))

#define SCRATCHSIZE 1024
#define ERRMSGSIZE 256
#define DEFROOTSIZE 18
#define DEFSWAPSIZE 16
#define DEFUSRSIZE 80
#define DEFFSIZE 1024
#define DEFFRAG 8

#define BOOT_MAGIC 0xAA55
#define ACTIVE 0x80

#define COPYRIGHT_FILE	"/COPYRIGHT"
#define README_FILE	"/README"
#define HELPME_FILE	"/DISKSPACE.FAQ"
#define TROUBLE_FILE	"/TROUBLESHOOTING"
#define RELNOTES_FILE	"/RELNOTES.FreeBSD"

#ifndef EXTERN
#  define EXTERN extern
#endif

/* All this "disk" stuff */
EXTERN int Ndisk;
EXTERN struct disklabel *Dlbl[MAX_NO_DISKS];
EXTERN char *Dname[MAX_NO_DISKS];
EXTERN int Dfd[MAX_NO_DISKS];

EXTERN int MP[MAX_NO_DISKS][MAXPARTITIONS];

/* All this "filesystem" stuff */
EXTERN int Nfs;
EXTERN char *Fname[MAX_NO_FS+1];
EXTERN char *Fmount[MAX_NO_FS+1];
EXTERN char *Ftype[MAX_NO_FS+1];
EXTERN u_long Fsize[MAX_NO_FS+1];

EXTERN int dialog_active;
EXTERN char selection[];
EXTERN int debug_fd;

EXTERN int no_mounts;
EXTERN struct fstab *mounts[MAX_NO_MOUNTS];

extern unsigned char *scratch;
extern unsigned char *errmsg;

extern u_short dkcksum(struct disklabel *);

/* utils.c */
int     strheight __P((const char *p));
int     strwidth __P((const char *p));
void	Abort __P((void));
void	ExitSysinstall __P((void));
void	TellEm __P((char *fmt, ...));
void	Debug __P((char *fmt, ...));
void	stage0	__P((void));
void	*Malloc __P((size_t size));
char	*StrAlloc __P((char *str));
void	Fatal __P((char *fmt, ...));
void	AskAbort __P((char *fmt, ...));
void	MountUfs __P((char *device, char *mountpoint, int do_mkdir,int flags));
void	Mkdir __P((char *path));
void	Link __P((char *from, char *to));
void	CopyFile __P((char *p1, char *p2));
u_long	PartMb(struct disklabel *lbl,int part);
char *	SetMount __P((int disk, int part, char *path));
void	CleanMount __P((int disk, int part));

/* exec.c */
int	exec __P((int magic, char *cmd, char *args, ...));
#define EXEC_MAXARG	100

/* stage0.c */
void	stage0 __P((void));

/* stage1.c */
int	stage1 __P((void));

/* stage2.c */
void	stage2 __P((void));

/* stage3.c */
void	stage3 __P((void));

/* stage4.c */
void	stage4 __P((void));

/* stage5.c */
void	stage5 __P((void));

/* termcap.c */
int	set_termcap __P((void));

/* makedevs.c */
int	makedevs __P((void));

/* ourcurses.c */
int AskEm __P((WINDOW *w,char *prompt, char *answer, int len));
void ShowFile __P((char *filename, char *header));

/* label.c */
int sectstoMb(int, int);
int Mb_to_cylbdry(int, struct disklabel *);
void default_disklabel(struct disklabel *, int, int);
int disk_size(struct disklabel *);

/* mbr.c */
int edit_mbr(int);
