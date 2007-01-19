/*   This is part of um-ViewOS
 *   The user-mode implementation of OSVIEW -- A Process with a View
 *
 *   umview.c: main
 *   
 *   Copyright 2005 Renzo Davoli University of Bologna - Italy
 *   Modified 2005 Ludovico Gardenghi, Andrea Gasparini, Andrea Seraghiti
 *   
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *   $Id$
 *
 */   
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <linux/sysctl.h>
#include "defs.h"
#include "umview.h"
#include "capture_sc.h"
#include "sctab.h"
#include "services.h"
#include "um_select.h"
#include "um_services.h"
#include "ptrace_multi_test.h"
#include "mainpoll.h"
#include "gdebug.h"

int _umview_version = 2; /* modules interface version id.
										modules can test to be compatible with
										um-viewos kernel*/
unsigned int has_ptrace_multi;
unsigned int ptrace_vm_mask;
unsigned int ptrace_viewos_mask;
unsigned int hasppoll;

unsigned int want_ptrace_multi, want_ptrace_vm, want_ptrace_viewos, want_ppoll;

extern int nprocs;

struct prelist {
	char *module;
	struct prelist *next;
};

/* module preload list */
static struct prelist *prehead=NULL;

/* add a module for pre-loading */
static void preadd(struct prelist **head,char *module)
{
	struct prelist *new=malloc(sizeof(struct prelist));
	assert(new);
	new->module=module;
	new->next=*head;
	*head=new;
}

/* virtual syscall for the underlying umview */
static long int_virnsyscall(long virscno,int n,long arg1,long arg2,long arg3,long arg4,long arg5,long arg6) {
	struct __sysctl_args scarg;
	long args[6]={arg1,arg2,arg3,arg4,arg5,arg6};
	scarg.name=NULL;
	scarg.nlen=virscno;
	scarg.oldval=NULL;
	scarg.oldlenp=NULL;
	scarg.newval=args;
	scarg.newlen=n;
	return native_syscall(__NR__sysctl,&scarg);
}

/* preload of modules */
static int do_preload(struct prelist *head)
{
	if (head != NULL) {
		void *handle;
		int rv=do_preload(head->next);
		handle=open_dllib(head->module);
		if (handle==NULL) {
			fprintf(stderr, "%s\n",dlerror());
			return -1;
		} else {
			set_handle_new_service(handle,0);
			return rv;
		}
		free(head);
	} else
		return 0;
}

/* preload for nexted umview (it is a burst of um_add_module) */
static int do_preload_recursive(struct prelist *head)
{
	if (head != NULL) {
		do_preload_recursive(head->next);
		int_virnsyscall(__NR_UM_SERVICE,3,ADD_SERVICE,0,(long)head->module,0,0,0);
		free(head);
		return 0;
	} else
		return 0;
}

static void version(int verbose)
{
	fprintf(stderr, "%s %s\n", UMVIEW_NAME, UMVIEW_VERSION);

	if (verbose)
		fprintf(stderr, "%s\n", UMVIEW_DESC);

	fprintf(stderr, "Copyright (C) %s\n", UMVIEW_COPYRIGHT);
	
	if (verbose)
		fprintf(stderr, "Development team:\n%s\n", UMVIEW_TEAM);

	fprintf(stderr, "%s\n\n", UMVIEW_URL);
	return;
}

static void usage(char *s)
{
	version(0);
	
	fprintf(stderr, "Usage: %s [OPTION] ... command [args]\n"
			"  -h, --help                print this help message\n"
			"  -v, --version             show version information\n"
			"  -p file, --preload file   load plugin named `file' (must be a .so)\n"
			"  -o file, --output file    send debug messages to file instead of stderr\n"
			"  -x, --nonesting           do not permit module nesting\n"
			"  -n, --nokernelpatch       avoid using kernel patches\n"
			"  --nokmulti                avoid using PTRACE_MULTI\n"
			"  --nokvm                   avoid using PTRACE_SYSVM\n"
			"  --nokviewos               avoid using PTRACE_VIEWOS\n\n"
			"  --noppoll                 avoid using ppoll\n\n",
			s);
	exit(0);
}

static struct option long_options[] = {
	{"preload",1,0,'p'},
	{"output",1,0,'o'},
	{"help",0,0,'h'},
	{"nonesting",0,0,'x'},
	{"nokernelpatch",0,0,'n'},
	{"nokmulti",0,0,0x100},
	{"nokvm",0,0,0x101},
	{"nokviewos",0,0,0x102},
	{"noppoll",0,0,0x103},
	{0,0,0,0}
};

/* pure_libc loading (by relaoding the entire umview) */
static void load_it_again(int argc,char *argv[])
{
	int nesting=1;
	while (1) {
		int c;
		int option_index = 0;
		/* some options must be parsed before reloading */
		c=getopt_long(argc,argv,"+p:o:hvnx",long_options,&option_index);
		if (c == -1) break;
		switch (c) {
			case 'h':
				usage(argv[0]);
				break;
			case 'v':
				version(1);
				exit(0);
				break;
			case 'x': /* do not use pure_libc */
				nesting=0;
				break;
		}
	}
	if (nesting) {
		char *path;
		void *handle;
		/* does pure_libc exist ? */
		if ((handle=dlopen("libpurelibc.so",RTLD_LAZY))!=NULL) {
			dlclose(handle);
			/* get the executable from /proc */
			asprintf(&path,"/proc/%d/exe",getpid());
			/* preload the pure_libc library */
			setenv("LD_PRELOAD","libpurelibc.so",1);
			/* reload the executable with a leading - */
			argv[0]="-umview";
			execv(path,argv);
			/* useless cleanup */
			free(path);
		}
	}
}

/* recursive umview invocation (umview started inside a umview machine) */
static void umview_recursive(int argc,char *argv[])
{
	fprintf(stderr,"UMView: nested invocation\n\n");
	while (1) {
		int c;
		int option_index = 0;
		c=getopt_long(argc,argv,"+p:o:hvnx",long_options,&option_index);
		if (c == -1) break;
		switch (c) {
			case 'h':
				usage(argv[0]);
				break;
			case 'v':
				version(1);
				exit(0);
				break;
			case 'p': 
				preadd(&prehead,optarg);
				break;
		}
	}
	do_preload_recursive(prehead);
	/* exec the process */
	execvp(*(argv+optind),argv+optind);
	exit(-1);
}

/*
static int has_pselect_test()
{
#ifdef _USE_PSELECT
	static struct timespec to={0,0};
	return (r_pselect6(0,NULL,NULL,NULL,&to,NULL)<0)?0:1;
#else
	return 0;
#endif
}
*/

#include<errno.h>
/* UMVIEW MAIN PROGRAM */
int main(int argc,char *argv[])
{
	/* try to set the priority to -11 provided umview has been installed
	 * setuid. it is effectiveless elsewhere */
	r_setpriority(PRIO_PROCESS,0,-11);
	/* if it was setuid, return back to the user status immediately,
	 * for safety! */
	r_setuid(getuid());
	/* if this is a nested invocation of umview, notify the umview monitor
	 * and execute the process, 
	 * try the nested invocation notifying virtual syscall, 
	 * if it succeeded it is actually a nested invocation,
	 * otherwise nobody is notified and the call fails*/
	if (int_virnsyscall(__NR_UM_SERVICE,1,RECURSIVE_UMVIEW,0,0,0,0,0) >= 0)
		umview_recursive(argc,argv);	/* do not return!*/
	/* umview loads itself twice if there is pure_libc, to trace module 
	 * generated syscalls, this condition manages the first call */
	if (strcmp(argv[0],"-umview")!=0)
		load_it_again(argc,argv);	/* do not return!*/
	/* does this kernel provide pselect? */
	/*has_pselect=has_pselect_test();*/
	optind=0;
	argv[0]="umview";
	/* set up the scdtab */
	scdtab_init();
	/* test the ptrace support */
	has_ptrace_multi=test_ptracemulti(&ptrace_vm_mask,&ptrace_viewos_mask);
	hasppoll=hasppolltest();
	want_ptrace_multi = has_ptrace_multi;
	want_ptrace_vm = ptrace_vm_mask;
	want_ptrace_viewos = ptrace_viewos_mask;
	want_ppoll = hasppoll;
	/* option management */
	while (1) {
		int c;
		int option_index = 0;
		c=getopt_long(argc,argv,"+p:o:hvnx",long_options,&option_index);
		if (c == -1) break;
		switch (c) {
			case 'h': /* help */
				usage(argv[0]);
				break;
			case 'v': /* version */
				version(1);
				exit(0);
				break;
			case 'p': /* module preload, here the module requests are just added to
			             a data structure */
				preadd(&prehead,optarg);
				break;
			case 'o': /* debugging output file redirection */ { 
						if (optarg==NULL){
							fprintf(stderr, "%s: must specify an argument after -o\n",argv[0]);
							break;
						}
						gdebug_set_ofile(optarg);
					 }
					 break;
			case 'n': /* do not use kernel extensions */
					 want_ptrace_multi = 0;
					 want_ptrace_vm = 0;
					 want_ptrace_viewos = 0;
					 break;
			case 0x100: /* do not use ptrace_multi */
					 want_ptrace_multi = 0;
					 break;
			case 0x101: /* do not use ptrace_vm */
					 want_ptrace_vm = 0;
					 break;
			case 0x102: /* do not use ptrace_viewos */
					 want_ptrace_viewos = 0;
					 break;
			case 0x103: /* do not use ppoll */
					 want_ppoll = 0;
					 break;
		}
	}
	
	if (has_ptrace_multi || ptrace_vm_mask || ptrace_viewos_mask || hasppoll)
	{
		fprintf(stderr, "This kernel supports: ");
		if (has_ptrace_multi)
			fprintf(stderr, "PTRACE_MULTI ");
		if (ptrace_vm_mask)
			fprintf(stderr, "PTRACE_SYSVM ");
		if (ptrace_viewos_mask)
			fprintf(stderr, "PTRACE_VIEWOS ");
		if (hasppoll)
			fprintf(stderr, "ppoll ");
		fprintf(stderr, "\n");
	}
	
	if (has_ptrace_multi || ptrace_vm_mask || ptrace_viewos_mask || hasppoll ||
			want_ptrace_multi || want_ptrace_vm || want_ptrace_viewos)
	{
		fprintf(stderr, "%s will use: ", UMVIEW_NAME);	
		if (want_ptrace_multi)
			fprintf(stderr,"PTRACE_MULTI ");
		if (want_ptrace_vm)
			fprintf(stderr,"PTRACE_SYSVM ");
		if (want_ptrace_viewos)
			fprintf(stderr,"PTRACE_VIEWOS ");
		if (want_ppoll)
			fprintf(stderr,"ppoll ");
		if (!want_ptrace_multi && !want_ptrace_vm && !want_ptrace_viewos && !want_ppoll)
			fprintf(stderr,"nothing");
		fprintf(stderr,"\n\n");
	}
	
	has_ptrace_multi = want_ptrace_multi;
	ptrace_vm_mask = want_ptrace_vm;
	ptrace_viewos_mask = want_ptrace_viewos;
	hasppoll = want_ppoll;
	
	if (hasppoll) {
		sigset_t unblockchild;
		sigprocmask(SIG_BLOCK,NULL,&unblockchild);
		pcb_inits(1);
		capture_main(argv+optind,1);
		do_preload(prehead);
		while (nprocs) {
			mp_ppoll(&unblockchild);
			tracehand();
		}
	}
	else {
		int wt=wake_tracer_init();
		/* logically should follow pcb_inits, but the wake tracer fd will be the
		 * most hit, so this inversion gives performance to the system */
		mp_add(wt,POLLIN,do_wake_tracer,NULL);
		pcb_inits(0);
		capture_main(argv+optind,0);
		do_preload(prehead);
		while (nprocs)  {
			mp_poll();
			do_wake_tracer();
		}
	}
	pcb_finis(hasppoll);
	return first_child_exit_status;
}
