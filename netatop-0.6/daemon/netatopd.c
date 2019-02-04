/*
** The netatopd daemon can be started optionally to read the counters
** of exited processes from the netatop module. The 'usual' state of this
** daemon is the getsockopt command NETATOP_GETCNT_EXIT. With this command
** netatopd blocks until an exited task is available on the exitlist of the 
** netatop module. The obtained netpertask struct is written to a logfile
** (similar to the process accounting info provided by the base kernel
** itself) when there is at least 5% of free space in the filesystem of
** the logfile.
**
** The logfile contains a small header struct that contains a.o. a sequence
** number indicating how many netpertask structs are currently logged.
** This header is mmapped and can be consulted by analysis processes that
** consult the logfile (e.g. atop).
** Behind the header, the netpertask structs can be found in compressed
** form. Every compressed netpertask struct is preceeded by one byte 
** specifying the size of the compressed netpertask struct.
**
** Before an analysis process starts using the logfile, it has to subscribe
** itself by decrementing the second semaphore of a semaphore group owned
** by netatopd. In this way netatopd knows how many analysis processes
** are using the logfile. When no processes use the logfile any more,
** netatopd removes the logfile and start building a new one as soon as
** a new subscription of an analysis process is noticed.
** ----------------------------------------------------------------------
** Copyright (C) 2012    Gerlof Langeveld (gerlof.langeveld@atoptool.nl)
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License version 2 as
** published by the Free Software Foundation.
*/
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>
#include <zlib.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "../netatop.h"
#include "../netatopd.h"
#include "../netatopversion.h"

#define	EVER	;;

#include <stdio.h>
#include <string.h>

static int	histopen(struct naheader **);
static void	recstore(int, struct netpertask *, socklen_t);

/*
** Semaphore-handling
**
** A semaphore-group with two semaphores is created. The first semaphore
** specifies the number of netatopd processes running (to be sure that only
** one daemon is active at the time) ) and the second reflects the number
** of processes using the log-file (inverted).
** This second semaphore is initialized at some high value and is
** decremented by every analysis process (like atop) that uses the log-file
** and incremented as soon as such analysis process stops again.
*/
#define SEMTOTAL        100
#define	NUMCLIENTS	(SEMTOTAL - semctl(semid, 1, GETVAL, 0))

int
main(int argc, char *argv[])
{
	int 			i, netsock, histfd = -1, semid;
	struct netpertask	npt;
	socklen_t 		len = sizeof npt;
	struct naheader 	*nap;
	struct sigaction        sigact;
	void			gethup(int);
	struct sembuf		semincr = {0, +1, SEM_UNDO};

	/*
	** version number required?
	*/
	if (argc == 2 && *argv[1] == '-' && *(argv[1]+1) == 'v')
	{
		printf("%s - %s     <gerlof.langeveld@atoptool.nl>\n",
			NETATOPVERSION, NETATOPDATE);
		return 0;
	}

	/*
 	** verify if we are running with the right privileges
	*/
	if (geteuid() != 0)
	{
		fprintf(stderr, "Root privileges are needed!\n");
		exit(1);
	}

	/*
 	** open socket to IP layer 
	*/
	if ( (netsock = socket(PF_INET, SOCK_RAW, IPPROTO_RAW)) == -1)
	{
		perror("open raw socket");
		exit(2);
	}

	/*
	** create the semaphore group and initialize it;
	** if it already exists, verify if a netatopd daemon
	** is already running
	*/
	if ( (semid = semget(SEMAKEY, 0, 0)) >= 0)	// exists?
	{
		if ( semctl(semid, 0, GETVAL, 0) == 1)
		{
			fprintf(stderr, "Another netatopd is already running!");
			exit(3);
		}
	}
	else
	{
		if ( (semid = semget(SEMAKEY, 2, 0600|IPC_CREAT|IPC_EXCL)) >= 0)
		{
			(void) semctl(semid, 0, SETVAL, 0);
			(void) semctl(semid, 1, SETVAL, SEMTOTAL);
		}
		else
		{
			perror("cannot create semaphore");
			exit(3);
		}
	}

	/*
	** daemonize this process
	*/
	if ( fork() )
		exit(0);	// implicitly switch to background

	setsid();

	if ( fork() )
		exit(0);

	for (i=0; i < 1024; i++)
		if (i != netsock)
			close(i);

	umask(022);

	chdir("/tmp");

	/*
	** open syslog interface for failure messages
	*/
	openlog("netatopd", LOG_PID, LOG_DAEMON);
	syslog(LOG_INFO, "version %s actived\n", NETATOPVERSION);

	/*
	** raise semaphore to define a busy netatopd
	*/
	if ( semop(semid, &semincr, 1) == -1)
       	{
		syslog(LOG_ERR, "cannot increment semaphore\n");
		exit(3);
	}

	/*
	** the daemon can be woken up from getsockopt by receiving 
	** the sighup signal to verify if there are no clients any more
	** (truncate exitfile)
	*/
        memset(&sigact, 0, sizeof sigact);
        sigact.sa_handler = gethup;
        sigaction(SIGHUP, &sigact, (struct sigaction *)0);

	/*
	** raise priority
	*/
	(void) nice(-39);

	/*
	** open history file
	*/
	histfd = histopen(&nap);

	/*
	** continuously obtain the info about exited processes
	*/
	for (EVER)
	{
		/*
		** check if anybody is interested in the exitfile
		** if not, close and truncate it
		*/
		if (NUMCLIENTS == 0 && nap->curseq != 0)
		{
			/*
			** destroy and reopen history file
			*/
			munmap(nap, sizeof(struct naheader));
			close(histfd);
			syslog(LOG_INFO, "reopen history file\n");
			histfd = histopen(&nap);
		}

		/*
 		** get next exited process (call blocks till available)
		*/
		switch (getsockopt(netsock, SOL_IP, NETATOP_GETCNT_EXIT,
								&npt, &len))
		{
		   case 0:
			// skip if nobody is using it
			if (NUMCLIENTS == 0)
				continue;

			/*
			** at least one listener is active, so
			** store record for exited process
			*/
			recstore(histfd, &npt, len);

			/*
			** increment sequence number in file header
			*/
			nap->curseq++;
			break;

		   default:	// getsockopt failed
			switch (errno)
			{
				// no netatop module loaded?
			   case ENOPROTOOPT:
				sleep(10);
				continue;

				// signal received?
			   case EINTR:
				continue;

			   default:
				syslog(LOG_ERR, "getsockopt failed\n");
				return 0;
			}
		}
	}

	return 0;
}

/*
** open history file
*/
static int
histopen(struct naheader **nahp)
{
	int			fd;
	struct naheader 	nahdr = {MYMAGIC, 0,
					sizeof(struct naheader),
		                	sizeof(struct netpertask),
					getpid()};
	/*
 	** remove the old file; this way atop can detect that a
	** new file must be opened
	*/
	(void) unlink(NETEXITFILE);

	/*
 	** open new file
	*/
	if ( (fd = open(NETEXITFILE, O_RDWR|O_CREAT|O_TRUNC, 0644)) == -1)
	{
		syslog(LOG_ERR, "cannot open %s for write\n", NETEXITFILE);
		exit(3);
	}

	/*
 	** write new header and mmap
	*/
	if ( write(fd, &nahdr, sizeof nahdr) != sizeof nahdr)
	{
		syslog(LOG_ERR, "cannot write to %s\n", NETEXITFILE);
		exit(3);
	}

	*nahp = mmap((void *)0, sizeof *nahp, PROT_WRITE, MAP_SHARED, fd, 0);

	if (*nahp == (void *) -1)
	{
		syslog(LOG_ERR, "mmap of %s failed\n", NETEXITFILE);
		exit(3);
	}

	return fd;
}

static void
recstore(int fd, struct netpertask *np, socklen_t len)
{
	Byte		compbuf[sizeof *np + 128];
	unsigned long	complen = sizeof compbuf -1;
	struct statvfs	statvfs;
	int		rv;

	/*
 	** check if the filesystem is not filled for more than 95%
	*/
	if ( fstatvfs(fd, &statvfs) != -1)
	{
		if (statvfs.f_bfree * 100 / statvfs.f_blocks < 5)
		{
			syslog(LOG_ERR, "Filesystem > 95%% full; "
			                "write skipped\n");
			return;
		}
	}

	/*
 	** filesystem space sufficient
	** compress netpertask struct
	*/
	rv = compress(compbuf+1, &complen, (Byte *)np,
					(unsigned long)sizeof *np);
	switch (rv)
	{
           case Z_OK:
           case Z_STREAM_END:
           case Z_NEED_DICT:
		break;

	   default:
		syslog(LOG_ERR, "compression failure\n");
		exit(5);
	}

	compbuf[0] = (Byte)complen;

	/*
	** write compressed netpertask struct, headed by one byte
	** with the size of the compressed struct
	*/
	if ( write(fd, compbuf, complen+1) < complen)
	{
		syslog(LOG_ERR, "write failure\n");
		exit(5);
	}
}

/*
** dummy handler for SIGHUP
*/
void
gethup(int sig)
{
}
