
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            main.c
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

#include "type.h"
#include "stdio.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"


/*****************************************************************************
 *                               kernel_main
 *****************************************************************************/
/**
 * jmp from kernel.asm::_start. 
 * 
 *****************************************************************************/
PUBLIC int kernel_main()
{
	disp_str("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
		 "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");

	int i, j, eflags, prio;
        u8  rpl;
        u8  priv; /* privilege */

	struct task * t;
	struct proc * p = proc_table;
	QUEUE*	p_queue	= queue_table;
	int*	p_prio	= priority_table;

	char * stk = task_stack + STACK_SIZE_TOTAL;

	for (i = 0; i < NR_QUEUE + 1; i++){
		p_queue->p_head = p_queue->p_tail = p_queue->buf;
		p_queue->priority = *p_prio;
		p_queue->count = 0;
		p_prio++;
		p_queue++;
	}
	

	for (i = 0; i < NR_TASKS + NR_PROCS; i++,p++,t++) {
		if (i >= NR_TASKS + NR_NATIVE_PROCS) {
			p->p_flags = FREE_SLOT;
			continue;
		}

	        if (i < NR_TASKS) {     /* TASK */
                        t	= task_table + i;
                        priv	= PRIVILEGE_TASK;
                        rpl     = RPL_TASK;
                        eflags  = 0x1202;/* IF=1, IOPL=1, bit 2 is always 1 */
			prio    = 15;
                }
                else {                  /* USER PROC */
                        t	= user_proc_table + (i - NR_TASKS);
                        priv	= PRIVILEGE_USER;
                        rpl     = RPL_USER;
                        eflags  = 0x202;	/* IF=1, bit 2 is always 1 */
			prio    = 5;
                }

		strcpy(p->name, t->name);	/* name of the process */
		p->p_parent = NO_TASK;

		if (strcmp(t->name, "INIT") != 0) {
			p->ldts[INDEX_LDT_C]  = gdt[SELECTOR_KERNEL_CS >> 3];
			p->ldts[INDEX_LDT_RW] = gdt[SELECTOR_KERNEL_DS >> 3];

			/* change the DPLs */
			p->ldts[INDEX_LDT_C].attr1  = DA_C   | priv << 5;
			p->ldts[INDEX_LDT_RW].attr1 = DA_DRW | priv << 5;
		}
		else {		/* INIT process */
			unsigned int k_base;
			unsigned int k_limit;
			int ret = get_kernel_map(&k_base, &k_limit);
			assert(ret == 0);
			init_desc(&p->ldts[INDEX_LDT_C],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_C | priv << 5);

			init_desc(&p->ldts[INDEX_LDT_RW],
				  0, /* bytes before the entry point
				      * are useless (wasted) for the
				      * INIT process, doesn't matter
				      */
				  (k_base + k_limit) >> LIMIT_4K_SHIFT,
				  DA_32 | DA_LIMIT_4K | DA_DRW | priv << 5);
		}

		p->regs.cs = INDEX_LDT_C << 3 |	SA_TIL | rpl;
		p->regs.ds =
			p->regs.es =
			p->regs.fs =
			p->regs.ss = INDEX_LDT_RW << 3 | SA_TIL | rpl;
		p->regs.gs = (SELECTOR_KERNEL_GS & SA_RPL_MASK) | rpl;
		p->regs.eip	= (u32)t->initial_eip;
		p->regs.esp	= (u32)stk;
		p->regs.eflags	= eflags;

		p->type= prio;

		p->p_flags = 0;
		p->p_msg = 0;
		p->p_recvfrom = NO_TASK;
		p->p_sendto = NO_TASK;
		p->has_int_msg = 0;
		p->q_sending = 0;
		p->next_sending = 0;

		for (j = 0; j < NR_FILES; j++)
			p->filp[j] = 0;

		stk -= t->stacksize;
		append_proc(p , queue_table);
	}

	k_reenter = 0;
	ticks = 0;

	p_proc_ready	= proc_table;

	init_clock();
        init_keyboard();

	restart();

	while(1){}
}


/*****************************************************************************
 *                                get_ticks
 *****************************************************************************/
PUBLIC int get_ticks()
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = GET_TICKS;
	send_recv(BOTH, TASK_SYS, &msg);
	return msg.RETVAL;
}


/**
 * @struct posix_tar_header
 * Borrowed from GNU `tar'
 */
struct posix_tar_header
{				/* byte offset */
	char name[100];		/*   0 */
	char mode[8];		/* 100 */
	char uid[8];		/* 108 */
	char gid[8];		/* 116 */
	char size[12];		/* 124 */
	char mtime[12];		/* 136 */
	char chksum[8];		/* 148 */
	char typeflag;		/* 156 */
	char linkname[100];	/* 157 */
	char magic[6];		/* 257 */
	char version[2];	/* 263 */
	char uname[32];		/* 265 */
	char gname[32];		/* 297 */
	char devmajor[8];	/* 329 */
	char devminor[8];	/* 337 */
	char prefix[155];	/* 345 */
	/* 500 */
};

/*****************************************************************************
 *                                untar
 *****************************************************************************/
/**
 * Extract the tar file and store them.
 * 
 * @param filename The tar file.
 *****************************************************************************/
void untar(const char * filename)
{
	printf("[extract `%s'\n", filename);
	int fd = open(filename, O_RDWR);
	assert(fd != -1);

	char buf[SECTOR_SIZE * 16];
	int chunk = sizeof(buf);

	while (1) {
		read(fd, buf, SECTOR_SIZE);
		if (buf[0] == 0)
			break;

		struct posix_tar_header * phdr = (struct posix_tar_header *)buf;

		/* calculate the file size */
		char * p = phdr->size;
		int f_len = 0;
		while (*p)
			f_len = (f_len * 8) + (*p++ - '0'); /* octal */

		int bytes_left = f_len;
		int fdout = open(phdr->name, O_CREAT | O_RDWR);
		if (fdout == -1) {
			printf("    failed to extract file: %s\n", phdr->name);
			printf(" aborted]");
			return;
		}
		printf("    %s (%d bytes)\n", phdr->name, f_len);
		while (bytes_left) {
			int iobytes = min(chunk, bytes_left);
			read(fd, buf,
			     ((iobytes - 1) / SECTOR_SIZE + 1) * SECTOR_SIZE);
			write(fdout, buf, iobytes);
			bytes_left -= iobytes;
		}
		close(fdout);
	}

	close(fd);

	printf(" done]\n");
}


/*****************************************************************************
 *                                ls
 *****************************************************************************/


void help()
{
	printf("========================================================================\n");
	printf("                          YBJ Operater System\n");
	printf("                       Build in September, 2017\n");
	printf("========================================================================\n");
	printf("Usage:\n");
	printf("  File:\n");
	printf("        ls                list directory contents\n");
	printf("        mkdir             make directories\n");
	printf("        create            create a new file\n");
	printf("        cd                enter a directories\n");
	printf("        rm                remove files or directories\n");
	printf("        rename            rename a file\n");
	printf("        read              read a file\n");
	printf("        edit             overwrite a file\n");
	printf("        edit+            write a file\n");
	printf("------------------------------------------------------------------------\n");
	printf("  System:\n");
	printf("        clear             clear the terminal screen\n");
	printf("        uname             print system information\n");
	printf("        help              display this help and exit\n");
	printf("        echo              display a line of text\n");
	printf("        reset             terminal initialization\n");
	printf("        pidof             find the process ID of a running program\n");
	printf("------------------------------------------------------------------------\n");
	printf("  Application:\n");
	printf("        queen             game\n");
	printf("        calculator        game\n");
	printf("========================================================================\n");

}




void clean()
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = CLEAN;
	msg.FD = 0;
	send_recv(SEND, TASK_FS, &msg);
//	return msg.RETVAL;

}

void reset()
{
	MESSAGE msg;
	reset_msg(&msg);
	msg.type = RESET;
	msg.FD = 0;
	send_recv(SEND, TASK_FS, &msg);
//	return msg.RETVAL;
}

//int get_path(char* ch)
//{
//	MESSAGE msg;
//	reset_msg(&msg);
//	msg.type = GET_PATH;
//	msg.FD = 0;
//	char a = 'a';
//	char* b = (char*)msg.BUF;
//	*b++ = a;
//	*b++ = a;
//	*b = 0;
//	printl("%s 0\n",msg.BUF);
//	send_recv(BOTH, TASK_FS, &msg);
//	printl("%s 4\n",msg.BUF);		
//	char* s  = (char*)msg.BUF;
//	while(*s)
//	{
//		*ch++ = *s++;
//	}
//	return msg.CNT;
//}


int pidof(char* p_name)
{
	struct proc*	p=proc_table;
	int i;
	for (i=0; i< NR_TASKS + NR_PROCS; i++,p++) 
	{
		if (p->p_flags == 0) 
		{
		//	printf("%s, %s, %d\n",p_name,p->name,strcmp(p_name,p->name));
			if(strcmp(p_name,p->name)==0)
				return i;
		}
	}
	return -1;
}

void fillpath(char* a,char* b)
{
	char* ch = a;
	char  path[MAX_PATH];
	char* s = path;
	char* c = b;
	if(*b == '.')
	{
		if(*(b + 1) == '.'&&*(b + 2) == '/')
		{
			if(strlen(a) == 1)
				return;
			b += 3;
			while(*ch){ ch++; }
			ch -= 2;
			while(*ch != '/') { ch--; }
			int i = 0;
			while(i < ch - a + 1)
			{
				*s++ = *(a + i);
				i++;
			}
			while(*b)
			{
				*s++ = *b++;
			}
			*s = 0;
		}
		else if(*(b + 1) == '.')
		{
			if(strlen(a) == 1)
				return;
			while(*ch){ ch++; }
			ch -= 2;
			while(*ch != '/') { ch--; }
			int i = 0;
			while(i < ch - a + 1)
			{
				*s++ = *(a + i);
				i++;
			}
			*s = 0;
		}
		else if(*( b + 1 ) == '/')
		{
			int i = 0;
			b += 2;
			while(*(a + i))
			{
				*s++ = *(a + i);
				i++;
			}
			while(*b)
			{
				*s++ = *b++;
			}
			*s = 0;
		}
		else{
			int i = 0;
			while(*(a + i))
			{
				*s++ = *(a + i);
				i++;
			}
			*s = 0;		
		}
	}
	else if(*b == '/'){
		return 0;
	}
	else{
		int i = 0;
		while(*(a + i))
		{
			*s++ = *(a + i);
			i++;
		}
		while(*b)
		{
			*s++ = *b++;
		}
		*s = 0;	
	}
	s = path;
	while(*s)
	{
		*c++ = *s++;
	}
	*c =0;
}


/*****************************************************************************
 *                                shabby_shell
 *****************************************************************************/
/**
 * A very very simple shell.
 * 
 * @param tty_name  TTY file name.
 *****************************************************************************/
void shabby_shell(const char * tty_name)
{
	int fd_stdin  = open(tty_name, O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open(tty_name, O_RDWR);
	assert(fd_stdout == 1);

	char rdbuf[128];
	char cmd[128];
    	char arg1[128];
    	char arg2[128];
    	char buffer[1024];
	char current_path[MAX_PATH] = "/";
	int length = strlen(current_path);
	help();
	//animation();
	while (1) {
		//length = get_path(current_path);
		write(1, current_path, length);
		write(1, "$ ", 2);
		//printf("%s, %d",current_path, length);
		eraseArray(&rdbuf,128);
		eraseArray(&cmd,128);
		eraseArray(&arg1,128);
		eraseArray(&arg2,128);
		eraseArray(buffer,1024);
		int r = read(0, rdbuf, 70);
		rdbuf[r] = 0;
		
		int argc = 0;
		char * argv[PROC_ORIGIN_STACK];
		char * p = rdbuf;
		char * s;
		int word = 0;
		char ch;
		
		do {
			ch = *p;
			if (*p != ' ' && *p != 0 && !word) {
				s = p;
				word = 1;
			}
			if ((*p == ' ' || *p == 0) && word) {
				word = 0;
				argv[argc++] = s;
				*p = 0;
			}
			p++;
		} while(ch);
		argv[argc] = 0;
		
		int fd = open(argv[0], O_RDWR);
		if (fd == -1) {
			if(rdbuf[0])
			{
				int i = 0;
				int j = 0;

				while (rdbuf[i] != ' ' && rdbuf[i] != 0)
				{
					cmd[i] = rdbuf[i];
					i++;
				}
				i++;

				while(rdbuf[i] != ' ' && rdbuf[i] != 0)
        			{
            				arg1[j] = rdbuf[i];
            				i++;
            				j++;
        			}
        			i++;
        			j = 0;

       				while(rdbuf[i] != ' ' && rdbuf[i] != 0)
        			{
            				arg2[j] = rdbuf[i];
            				i++;
            				j++;
        			}
				if(strcmp(cmd, "help") == 0)
				{
					
					help();
				}
				else if(strcmp(cmd, "clear") == 0)
				{
					clean();
				}
				else if(strcmp(cmd, "reset") == 0)
				{
					reset();
				}
				else if(strcmp(cmd, "uname") == 0)
				{
					printf("YBJ Operater System\n");
				}
				else if(strcmp(cmd, "pidof") == 0)
				{
					int pid=pidof(arg1);
					if(pid==-1)
						printf("There is no such process named %s!\n",arg1);
					else
						printf("%d\n",pid);
				}
				else if(strcmp(cmd, "ls") == 0)
				{
					fillpath(current_path, arg1);
					if(show_dir(buffer , arg1) != -1)
						printf("%s\n",buffer);
					else
						printf("ls: %s: No such file or directory\n",arg1);
				}
				else if(strcmp(cmd, "create") == 0)
				{
					fillpath(current_path, arg1);
					fd = open(arg1, O_CREAT | O_CAM);
					if(fd != -1){
						printf("File created: %s (fd: %d)\n", arg1,fd);
						//close(fd);
					}
					if(fd == -1){
						printf("create: cannot create file %s: File exists\n",arg1);
					}
					if(fd == -2){
						printf("create: cannot create file %s: File name error\n",arg1);					
					}
				}
				else if(strcmp(cmd, "mkdir") == 0)
				{
					fillpath(current_path, arg1);
					fd = open(arg1, O_CREAT | O_DIR);
					if(fd >= 0)
						printf("Floder created: %s\n",arg1);
					if(fd == -1)
						printf("mkdir: cannot create directory %s: File exists\n",arg1);
					if(fd == -2){
						printf("create: cannot create file %s: File name error\n",arg1);					
					}
				}
				else if(strcmp(cmd, "rm") == 0)
				{
					fillpath(current_path, arg1);
					fd = unlink(arg1);
					if(fd != -1){
						printf("File deleted: %s (fd: %d)\n", arg1,fd);
						//close(fd);
					}
					if(fd == -1){
						printf("rm: %s: No such file or it is a directory\n",arg1);
					}
				}		
				else if(strcmp(cmd, "edit+") == 0)
				{
					fillpath(current_path, arg1);
					fd = open(arg1, O_RDWR);
					if(fd >= 0){
						write(fd,arg2,-strlen(arg2));						
						printf("File %s writed:%s  (fd: %d,length: %d)\n", arg1,arg2,fd, strlen(arg2));
						close(fd);
					}
					if(fd == -1){
						printf("edit+: %s: you cannot edit this file\n",arg1);
					}
				}
				else if(strcmp(cmd, "edit") == 0)
				{
					fillpath(current_path, arg1);
					fd = open(arg1, O_RDWR);
					if(fd >= 0){
						write(fd,arg2,strlen(arg2));						
						printf("File %s writed:%s  (fd: %d,length: %d)\n", arg1,arg2,fd, strlen(arg2));
						close(fd);
					}
					if(fd == -1){
						printf("edit: %s: you cannot edit this file\n",arg1);
					}
				}
				else if(strcmp(cmd, "read") == 0)
				{
					fillpath(current_path, arg1);
					fd = open(arg1, O_RDWR);
					if(fd >= 0){
						int r = read(fd,buffer,-1);
						buffer[r] = 0;				
						printf("File %s read:%s  (fd: %d)\n", arg1,buffer,fd);
						close(fd);
					}
					if(fd == -1){
						printf("read: %s: you cannot read this file\n",arg1);
					}
				}
				else if(strcmp(cmd, "rename") == 0)
				{
					fillpath(current_path, arg1);
					int temp = rename(arg1,arg2);						
					if(temp == -1){
						printf("cd: %s: No such file or directory\n",arg1);
					}
					else					
						printf("File %s rename:%s\n", arg1,arg2);
					
				}
				else if(strcmp(cmd, "cd") == 0)
				{
					fillpath(current_path, arg1);
					//printf("%s\n",arg1);
					if(show_dir(buffer , arg1) != -1){
						char* ch = current_path;
						char* s = arg1;
						while(*s)
						{
							*ch++ = *s++;
						}
						if(*(ch - 1) != '/')
							*ch++ = '/';
						*ch = 0;
						length = strlen(current_path);	
					}
					else
						printf("cd: %s: No such file or directory\n",arg1);
				}
				else
				{
					write(1, rdbuf, r);
					printf(": command not find, use 'help' for help\n");
				}
				
			}
			
		}
		else {
			close(fd);
			int pid = fork();
			if (pid != 0) { /* parent */
				int s;
				wait(&s);
			}
			else {	/* child */
				execv(argv[0], argv);
			}
		}
	}

	close(1);
	close(0);
}



void eraseArray(char *arr, int length)
{
    int i;
    for (i = 0; i < length; i++)
        arr[i] = 0;
}



/*****************************************************************************
 *                                Init
 *****************************************************************************/
/**
 * The hen.
 * 
 *****************************************************************************/
void Init()
{
	int fd_stdin  = open("/dev_tty0", O_RDWR);
	assert(fd_stdin  == 0);
	int fd_stdout = open("/dev_tty0", O_RDWR);
	assert(fd_stdout == 1);

	printf("Init() is running ...\n");

	/* extract `cmd.tar' */
	untar("/cmd.tar");
			

	char * tty_list[] = {"/dev_tty1", "/dev_tty2"};

	int i;
	for (i = 0; i < sizeof(tty_list) / sizeof(tty_list[0]); i++) {
		int pid = fork();
		if (pid != 0) { /* parent process */
			printf("[parent is running, child pid:%d]\n", pid);
		}
		else {	/* child process */
			printf("[child is running, pid:%d]\n", getpid());
			close(fd_stdin);
			close(fd_stdout);
			
			shabby_shell(tty_list[i]);
			assert(0);
		}
	}
	//animation();

	while (1) {
		int s;
		int child = wait(&s);
		printf("child (%d) exited with status: %d.\n", child, s);
	}

	assert(0);
}


/*======================================================================*
                               TestA
 *======================================================================*/
void TestA()
{
	for(;;);
}

/*======================================================================*
                               TestB
 *======================================================================*/
void TestB()
{
	for(;;);
}

/*======================================================================*
                               TestB
 *======================================================================*/
void TestC()
{
	for(;;);
}

/*****************************************************************************
 *                                panic
 *****************************************************************************/
PUBLIC void panic(const char *fmt, ...)
{
	int i;
	char buf[256];

	/* 4 is the size of fmt in the stack */
	va_list arg = (va_list)((char*)&fmt + 4);

	i = vsprintf(buf, fmt, arg);

	printl("%c !!panic!! %s", MAG_CH_PANIC, buf);

	/* should never arrive here */
	__asm__ __volatile__("ud2");
}

