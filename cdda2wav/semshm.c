/* @(#)semshm.c	1.4 00/02/17 Copyright 1998,1999,2000 Heiko Eissfeldt */
#ifndef lint
static char     sccsid[] =
"@(#)semshm.c	1.4 00/02/17 Copyright 1998,1999,2000 Heiko Eissfeldt";

#endif
#define IPCTST
#undef IPCTST
/* -------------------------------------------------------------------- */
/*        semshm.c                                                      */
/* -------------------------------------------------------------------- */
/*               int seminstall(key,amount)                             */
/*               int semrequest(semid,semnum)                           */
/*               int semrelease(semid,semnum)                           */
/* -------------------------------------------------------------------- */

#include "config.h"

#if     !defined(HAVE_SMMAP) && !defined(HAVE_USGSHM) && !defined(HAVE_DOSALLOCSHAREDMEM)
#undef  FIFO                    /* We cannot have a FIFO on this platform */
#endif

#if !defined(USE_MMAP) && !defined(USE_USGSHM)
#define USE_MMAP
#endif

#ifndef HAVE_SMMAP
#       undef   USE_MMAP
#       define  USE_USGSHM      /* SYSV shared memory is the default    */
#endif

#ifdef  USE_MMAP                /* Only want to have one implementation */
#       undef   USE_USGSHM      /* mmap() is preferred                  */
#endif

#ifdef	HAVE_DOSALLOCSHAREDMEM
#       undef   USE_MMAP
#       undef   USE_USGSHM
#	define	USE_OS2SHM
#endif

#include <stdio.h>
#include <stdlib.h>

#if defined (HAVE_UNISTD_H) && (HAVE_UNISTD_H == 1)
#include <sys/types.h>
#include <unistd.h>
#endif

#include <fctldefs.h>
#include <errno.h>
#include <standard.h>

#if defined(HAVE_SEMGET) && defined(USE_SEMAPHORES)
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#endif

#ifdef  USE_USGSHM
#if defined(HAVE_SHMAT) && (HAVE_SHMAT == 1)
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif
#endif

#ifdef  USE_MMAP
#if defined(HAVE_SMMAP) && defined(USE_MMAP)
#include <sys/mman.h>
#endif
#endif

#include <scg/scsitransp.h>

#include "mytype.h"
#include "interface.h"
#include "ringbuff.h"
#include "global.h"
#include "semshm.h"

#ifdef DEBUG_SHM
char *start_of_shm;
char *end_of_shm;
#endif

int flush_buffers __PR((void));


/*------ Semaphore interfacing (for special cases only) ----------*/
/*------ Synchronization with pipes is preferred        ----------*/

#if defined(HAVE_SEMGET) && defined(USE_SEMAPHORES)

int sem_id;
static int seminstall	__PR((key_t key, int amount));

static int seminstall(key, amount)
	key_t key;
	int amount;
{
  int           ret_val;
  int           semflag;

  semflag = IPC_CREAT | 0600;
#ifdef IPCTST
  fprintf(stderr,"seminstall: key: %d, #sems %d, flags %4x\n",
          key,amount,semflag);
#endif
  ret_val = semget(key,amount,semflag);
  if ( ret_val == -1 )
  {
    fprintf(stderr,"semget: (Key %lx, #%d) failed: ",
            (long)key,amount);
    perror("");
  }
  return ret_val;
}

/*-----------------------------------------------------------------*/
int semrequest(semid, semnum)
	int semid;
	int semnum;
{
  struct sembuf sops[1];
  int    ret_val;

#ifdef IPCTST
  fprintf(stderr,"pid %d, ReQuest id:num %d:%d\n",getpid(),semid,semnum);
#endif
  sops[0].sem_op  = -1;
  sops[0].sem_num = (short) semnum;
  sops[0].sem_flg = 0;

  do {
    errno = 0;
    ret_val = semop(semid,sops,1);
    if (ret_val == -1 && errno != EAGAIN && errno != EINTR)
      {
	fprintf(stderr,"Request Sema %d(%d) failed: ",semid,semnum);
	perror("");
      }
  } while (errno == EAGAIN || errno == EINTR);
  return(ret_val);
}

/*-----------------------------------------------------------------*/
int semrelease(semid, semnum, amount)
	int semid;
	int semnum;
	int amount;
{
  struct sembuf sops[1];
  int    ret_val;

#ifdef IPCTST
  fprintf(stderr,"%d RL %d:%d\n",getpid(),semid,semnum);
#endif
  sops[0].sem_op  = amount;
  sops[0].sem_num = (short) semnum;
  sops[0].sem_flg = 0;
  ret_val = semop(semid,sops,1);
  if ( ret_val == -1 && errno != EAGAIN)
  {
    fprintf(stderr,"Release Sema %d(%d) failed: ",semid,semnum);
    perror("");
  }
  return(ret_val);
}

int flush_buffers()
{
	return 0;
}
#else
/*------ Synchronization with pipes ----------*/
int pipefdp2c[2];
int pipefdc2p[2];

void init_pipes()
{
  if (pipe(pipefdp2c) < 0) {
    perror("cannot create pipe parent to child");
    exit(1);
  }
  if (pipe(pipefdc2p) < 0) {
    perror("cannot create pipe child to parent");
    exit(1);
  }
}

void init_parent()
{
  close(pipefdp2c[0]);
  close(pipefdc2p[1]);
}

void init_child()
{
  close(pipefdp2c[1]);
  close(pipefdc2p[0]);
}

int semrequest(dummy, semnum)
	int dummy;
	int semnum;
{

  if (semnum == FREE_SEM /* 0 */)  {
      int retval;
    if ((*total_segments_read) - (*total_segments_written) >= global.buffers) {
      /* parent/reader waits for freed buffers from the child/writer */
      *parent_waits = 1;
      retval = read(pipefdp2c[0], &dummy, 1) != 1;
      return retval;
    }
  } else {
      int retval;

    if ((*total_segments_read) == (*total_segments_written)) {
      /* child/writer waits for defined buffers from the parent/reader */
      *child_waits = 1;
      retval = read(pipefdc2p[0], &dummy, 1) != 1;
      return retval;
    }
  }
  return 0;
}

int semrelease(dummy, semnum, amount)
	int dummy;
	int semnum;
	int amount;
{

  PRETEND_TO_USE(dummy);

  if (semnum == FREE_SEM /* 0 */)  {
    if (*parent_waits == 1) {
      int retval;
      /* child/writer signals freed buffer to the parent/reader */
      *parent_waits = 0;
      retval = write(pipefdp2c[1], "12345678901234567890", amount) != amount;
      return retval;
    }
  } else {
    if (*child_waits == 1) {
      int retval;
      /* parent/reader signals defined buffers to the child/writer */
      *child_waits = 0;
      retval = write(pipefdc2p[1], "12345678901234567890", amount) != amount;
      return retval;
    }
  }
  return 0;
}

int flush_buffers()
{
	if ((*total_segments_read) > (*total_segments_written)) {
		return write(pipefdc2p[1], "1", 1) != 1;
	}
	return 0;
}

#endif

/*------------------- Shared memory interfacing -----------------------*/



#ifdef  USE_USGSHM
#if defined(HAVE_SHMAT) && (HAVE_SHMAT == 1)
static int shm_request	__PR((unsigned int size, unsigned char **memptr));

/* request a shared memory block */
static int shm_request(size, memptr)
	unsigned int size;
	unsigned char **memptr;
{
  int   ret_val;
  int   shmflag;
  int   SHMEM_ID;
  int    cmd;
  struct shmid_ds buf;
  key_t key = IPC_PRIVATE;

  shmflag = IPC_CREAT | 0600;
  ret_val = shmget(key,(int)size,shmflag);
  if ( ret_val == -1 )
  {
    perror("shmget");
    return -1;
  }

  SHMEM_ID = ret_val;
  cmd = IPC_STAT;
  ret_val = shmctl(SHMEM_ID,cmd,&buf);
#ifdef IPCTST
  fprintf(stderr, "%d: shmctl STAT= %d, SHM_ID: %d, key %ld cuid %d cgid %d mode %3o size %d\n",
          getpid(),ret_val,SHMEM_ID,
          (long) buf.shm_perm.key,buf.shm_perm.cuid,buf.shm_perm.cgid,
          buf.shm_perm.mode,buf.shm_segsz);
#endif

  *memptr = (unsigned char *) shmat(SHMEM_ID, NULL, 0);
  if (*memptr == (unsigned char *) -1) {
    *memptr = NULL;
    fprintf( stderr, "shmat failed for %u bytes\n", size);
    return -1;
  }

  if (shmctl(SHMEM_ID, IPC_RMID, 0) < 0) {
    fprintf( stderr, "shmctl failed to detach shared memory segment\n");
    return -1;
  }


#ifdef DEBUG_SHM
  start_of_shm = *memptr;
  end_of_shm = (char *)(*memptr) + size;

  fprintf(stderr, "Shared memory from %p to %p (%u bytes)\n", start_of_shm, end_of_shm, size);
#endif
  return 0;
}
#endif
#endif

/* release semaphores */
void free_sem __PR(( void ));
void free_sem()
{
#if defined(HAVE_SEMGET) && defined(USE_SEMAPHORES)
  int   mycmd;
  union my_semun unused_arg;

  mycmd = IPC_RMID;

  /* HP-UX warns here, but 'unused_arg' is not used for this operation */
  /* This warning is difficult to avoid, since the structure of the union
   * generally is not known (os dependent). So we cannot initialize it
   * properly.
   */
  semctl(sem_id,0,mycmd,unused_arg);
#endif

}

#ifdef  USE_MMAP
#if defined(HAVE_SMMAP) && defined(USE_MMAP)
static int shm_request	__PR((unsigned int size, unsigned char **memptr));

int shm_id;
/* request a shared memory block */
static int shm_request(size, memptr)
	unsigned int size;
	unsigned char **memptr;
{
	int     f;
	char    *addr;

#ifdef  MAP_ANONYMOUS   /* HP/UX */
	f = -1;
	addr = mmap(0, (int) size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, f, 0);
#else
	if ((f = open("/dev/zero", O_RDWR)) < 0)
		comerr("Cannot open '/dev/zero'.\n");
	addr = mmap(0, (int) size, PROT_READ|PROT_WRITE, MAP_SHARED, f, 0);
#endif

	if (addr == (char *)-1)
		comerr("Cannot get mmap for %u Bytes on /dev/zero.\n", size);
	close(f);

	if (memptr != NULL)
		*memptr = (unsigned char *)addr;

	return 0;
}
#endif
#endif

#ifdef	USE_OS2SHM
static int shm_request	__PR((unsigned int size, unsigned char **memptr));

/* request a shared memory block */
static int shm_request(size, memptr)
	unsigned int size;
	unsigned char **memptr;
{
	char    *addr;

	/*
	 * The OS/2 implementation of shm (using shm.dll) limits the size of one         * memory segment to 0x3fa000 (aprox. 4MBytes). Using OS/2 native API we         * no such restriction so I decided to use it allowing fifos of arbitrar         */
	if(DosAllocSharedMem(&addr,NULL,size,0X100L | 0x1L | 0x2L | 0x10L))
      		comerr("DosAllocSharedMem() failed\n");

	if (memptr != NULL)
		*memptr = (unsigned char *)addr;

	return 0;
}
#endif

void *request_shm_sem(amount_of_sh_mem, pointer)
	unsigned amount_of_sh_mem;
	unsigned char **pointer;
{
#if defined(HAVE_SEMGET) && defined(USE_SEMAPHORES)
    /* install semaphores for double buffer usage */
    sem_id = seminstall(IPC_PRIVATE,2);
    if ( sem_id == -1 ) {
      perror("seminstall");
      exit(1);
    }

#endif

#if defined(FIFO)
    if (-1 == shm_request(amount_of_sh_mem, pointer)) {
	perror("shm_request");
	exit(1);
    }

    return *pointer;
#else
    return NULL;
#endif
}