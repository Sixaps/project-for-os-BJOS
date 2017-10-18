/*************************************************************************//**
 *****************************************************************************
 * @file   misc.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   2008
 *****************************************************************************
 *****************************************************************************/

/* Orange'S FS */

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
#include "keyboard.h"
#include "proto.h"
#include "hd.h"
#include "fs.h"

/*****************************************************************************
 *                                do_stat
 *************************************************************************//**
 * Perform the stat() syscall.
 * 
 * @return  On success, zero is returned. On error, -1 is returned.
 *****************************************************************************/
PUBLIC int do_stat()
{
	char pathname[MAX_PATH]; /* parameter from the caller */
	char filename[MAX_PATH]; /* directory has been stipped */

	/* get parameters from the message */
	int name_len = fs_msg.NAME_LEN;	/* length of filename */
	int src = fs_msg.source;	/* caller proc nr. */
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),    /* to   */
		  (void*)va2la(src, fs_msg.PATHNAME), /* from */
		  name_len);
	pathname[name_len] = 0;	/* terminate the string */

	int inode_nr = search_file(pathname);
	if (inode_nr == INVALID_INODE) {	/* file not found */
		printl("{FS} FS::do_stat():: search_file() returns "
		       "invalid inode: %s\n", pathname);
		return -1;
	}

	struct inode * pin = 0;

	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0) {
		/* theoretically never fail here
		 * (it would have failed earlier when
		 *  search_file() was called)
		 */
		assert(0);
	}
	pin = get_inode(dir_inode->i_dev, inode_nr);

	struct stat s;		/* the thing requested */
	s.st_dev = pin->i_dev;
	s.st_ino = pin->i_num;
	s.st_mode= pin->i_mode;
	s.st_rdev= is_special(pin->i_mode) ? pin->i_start_sect : NO_DEV;
	s.st_size= pin->i_size;

	put_inode(pin);

	phys_copy((void*)va2la(src, fs_msg.BUF), /* to   */
		  (void*)va2la(TASK_FS, &s),	 /* from */
		  sizeof(struct stat));

	return 0;
}

/*****************************************************************************
 *                                search_file
 *****************************************************************************/
/**
 * Search the file and return the inode_nr.
 *
 * @param[in] path The full path of the file to search.
 * @return         Ptr to the i-node of the file if successful, otherwise zero.
 * 
 * @see open()
 * @see do_open()
 *****************************************************************************/
PUBLIC int search_file(char * path)
{
	int i, j;

	char filename[MAX_PATH];
	memset(filename, 0, MAX_FILENAME_LEN);
	struct inode * dir_inode;
	if (strip_path(filename, path, &dir_inode) != 0)
		return 0;

	if (filename[0] == 0)	/* path: "/" */
		return dir_inode->i_num;

	/**
	 * Search the dir for the file.
	 */
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	int nr_dir_entries =
	  dir_inode->i_size / DIR_ENTRY_SIZE; /**
					       * including unused slots
					       * (the file has been deleted
					       * but the slot is still there)
					       */
	int m = 0;
	struct dir_entry * pde;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);
		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {
			if (memcmp(filename, pde->name, MAX_FILENAME_LEN) == 0)
				return pde->inode_nr;
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries) /* all entries have been iterated */
			break;
	}

	/* file not found */
	return 0;
}

/*****************************************************************************
 *                                strip_path
 *****************************************************************************/
/**
 * Get the basename from the fullpath.
 *
 * In Orange'S FS v1.0, all files are stored in the root directory.
 * There is no sub-folder thing.
 *
 * This routine should be called at the very beginning of file operations
 * such as open(), read() and write(). It accepts the full path and returns
 * two things: the basename and a ptr of the root dir's i-node.
 *
 * e.g. After stip_path(filename, "/blah", ppinode) finishes, we get:
 *      - filename: "blah"
 *      - *ppinode: root_inode
 *      - ret val:  0 (successful)
 *
 * Currently an acceptable pathname should begin with at most one `/'
 * preceding a filename.
 *
 * Filenames may contain any character except '/' and '\\0'.
 *
 * @param[out] filename The string for the result.
 * @param[in]  pathname The full pathname.
 * @param[out] ppinode  The ptr of the dir's inode will be stored here.
 * 
 * @return Zero if success, otherwise the pathname is not valid.
 *****************************************************************************/
PUBLIC int strip_path(char * filename, const char * pathname,
		      struct inode** ppinode)
{
	const char * s = pathname;
	char * t = filename;
	int num = 0;
	int inode_nr;
	struct inode* pi = root_inode;
	
	if (s == 0)
		return -1;

	if (*s == '/')
		s++;
	//printl("%s\n",pathname);
	while (*s) {		/* check each character */
		if (*s == '/'){
			*t = 0;
			printl("%s\n",filename,strlen(filename));
			if(inode_nr = search_from_dir(filename, pi)){
				pi = get_inode(pi->i_dev,inode_nr);
				//pi->i_cnt--;			
			}
			else
				return -1;			
			//assert(inode_nr != 0);
			//printl("%d\n",inode_nr);
			while(t - filename >= 0){
				*t = 0;
				t--;
			}
			t = filename;
			s++;
			continue;
		}
		*t++ = *s++;
		//printl("%c", *(t - 1));
		/* if filename is too long, just truncate it */
		if (t - filename >= MAX_FILENAME_LEN)
			break;
	}
	*t = 0;
	*ppinode = pi;
	
	return 0;
}

PUBLIC int search_from_dir(char* filename, struct inode* dir_inode)
{
	int i, j;
	
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	int nr_dir_entries =
	  dir_inode->i_size / DIR_ENTRY_SIZE; /**
					       * including unused slots
					       * (the file has been deleted
					       * but the slot is still there)
					       */
	int m = 0;
	struct dir_entry * pde;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);
		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {			
			if (strcmpN(filename, pde->name, MAX_FILENAME_LEN) == 1 && pde->type == I_DIRECTORY)
				return pde->inode_nr;
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries) /* all entries have been iterated */
			break;
	}
	return 0;
}

PUBLIC int do_show_dir()
{
	char pathname[MAX_PATH];
	char filename[MAX_PATH];
	void * buf = fs_msg.BUF;
	struct inode* p_inode = root_inode;
	
	int cnt = 0;
	int off = 0;
	/* get parameters from the message */
	int flags = fs_msg.FLAGS;	/* access mode */
	int name_len = fs_msg.NAME_LEN;	/* length of filename */
	int src = fs_msg.source;	/* caller proc nr. */
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),
		  (void*)va2la(src, fs_msg.PATHNAME),
		  name_len);
	pathname[name_len] = 0;
	if(name_len == 1){
		int i, j;
	
		int dir_blk0_nr = p_inode->i_start_sect;
		int nr_dir_blks = (p_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
		int nr_dir_entries =
	  		p_inode->i_size / DIR_ENTRY_SIZE; /**
							       * including unused slots
							       * (the file has been deleted
							       * but the slot is still there)
							       */
		int m = 0;
		char* temp;
		struct dir_entry * pde;
		for (i = 0; i < nr_dir_blks; i++) {
			RD_SECT(p_inode->i_dev, dir_blk0_nr + i);
			pde = (struct dir_entry *)fsbuf;
			for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) 
			{	
				if(pde->inode_nr != 0){
					temp = pde->name;				
					phys_copy((void*)va2la(src, buf + off),
					  (void*)va2la(TASK_FS, temp),
					  strlen(temp));
					off += strlen(temp);
					*temp = ' '; 
					phys_copy((void*)va2la(src, buf + off),
					  (void*)va2la(TASK_FS, temp),
					  1);
					off++;
				}
				if (++m > nr_dir_entries)
					break;
			}
			if (m > nr_dir_entries) /* all entries have been iterated */
				break;
		}
		return 0;	
	}
	if(pathname[name_len-1] == '/')
		name_len--;
	pathname[name_len] = 0;
	
	int inode_nr = (pathname);
	
	if (strip_path(filename, pathname, &p_inode) != 0){
		printl("show_dir() error: not such directory\n");		
		return -1;
	}
	if(inode_nr = search_from_dir(filename, p_inode)){
		p_inode = get_inode(p_inode->i_dev,inode_nr);		
	}
	else{
		printl("show_dir() error: not such directory\n");	
		return -1;
	}
	int i, j;
	
	int dir_blk0_nr = p_inode->i_start_sect;
	int nr_dir_blks = (p_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	int nr_dir_entries =
  		p_inode->i_size / DIR_ENTRY_SIZE; /**
						       * including unused slots
						       * (the file has been deleted
						       * but the slot is still there)
						       */
	int m = 0;
	char* temp;
	struct dir_entry * pde;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(p_inode->i_dev, dir_blk0_nr + i);
		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) 
		{	
			if(pde->inode_nr != 0){
				temp = pde->name;				
				phys_copy((void*)va2la(src, buf + off),
				  (void*)va2la(TASK_FS, temp),
				  strlen(temp));
				off += strlen(temp);
				*temp = ' '; 
				phys_copy((void*)va2la(src, buf + off),
				  (void*)va2la(TASK_FS, temp),
				  1);
				off++;
				//printl("%s, %d\n",temp,strlen(temp));
			}
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries) /* all entries have been iterated */
			break;
	}
	return 0;
}

PUBLIC int strcmpN(char* a, char* b, int length)
{
	if(strlen(a) != strlen(b))
		return 0;	
	for(int i = 0;i < length ;i++)
	{
		if(*a == 0 && *b == 0)
			return 1;
		else if(*a == 0 || *b == 0)
			return 0;

		if(*a != *b)
			return 0;
	}
	return 1;
}

PUBLIC void do_cnrt()
{
	int fd = fs_msg.FD;	/**< file descriptor. */

	int src = fs_msg.source;		/* caller proc nr. */

	assert((pcaller->filp[fd] >= &f_desc_table[0]) &&
	       (pcaller->filp[fd] < &f_desc_table[NR_FILE_DESC]));

	if (!(pcaller->filp[fd]->fd_mode & O_RDWR))
		return 0;

	struct inode * pin = pcaller->filp[fd]->fd_inode;

	assert(pin >= &inode_table[0] && pin < &inode_table[NR_INODE]);

	int imode = pin->i_mode & I_TYPE_MASK;

	if (imode == I_CHAR_SPECIAL) {
		int t = fs_msg.type == CLEAN ? DEV_CLEAN : DEV_RESET;		
		fs_msg.type = t;

		int dev = pin->i_start_sect;
		assert(MAJOR(dev) == 4);

		fs_msg.DEVICE	= MINOR(dev);
		fs_msg.PROC_NR	= src;
		assert(dd_map[MAJOR(dev)].driver_nr != INVALID_DRIVER);
		send_recv(BOTH, TASK_TTY, &fs_msg);
		//assert(0);
	}
}

PUBLIC int do_rename()
{
	char pathname[MAX_PATH];

	/* get parameters from the message */
	int flags = fs_msg.FLAGS;	/* access mode */
	int name_len = fs_msg.NAME_LEN;	/* length of filename */
	int buf_len = fs_msg.BUF_LEN;
	int src = fs_msg.source;	/* caller proc nr. */
	char buf[MAX_PATH];
	assert(name_len < MAX_PATH);
	phys_copy((void*)va2la(TASK_FS, pathname),
		  (void*)va2la(src, fs_msg.PATHNAME),
		  name_len);
	phys_copy((void*)va2la(TASK_FS, buf),
		  (void*)va2la(src, fs_msg.BUF),
		  buf_len);
	pathname[name_len] = 0;
	buf[buf_len] = 0;
	//printl("%s,%d, %s\n",pathname,name_len,buf);
	
	int i, j;

	char filename[MAX_PATH];
	memset(filename, 0, MAX_FILENAME_LEN);
	struct inode * dir_inode;
	if (strip_path(filename, pathname, &dir_inode) != 0)
		return -1;

	if (filename[0] == 0)	/* path: "/" */
		return -1;

	/**
	 * Search the dir for the file.
	 */
	int dir_blk0_nr = dir_inode->i_start_sect;
	int nr_dir_blks = (dir_inode->i_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	int nr_dir_entries =
	  dir_inode->i_size / DIR_ENTRY_SIZE; /**
					       * including unused slots
					       * (the file has been deleted
					       * but the slot is still there)
					       */
	int m = 0;
	struct dir_entry * pde;
	for (i = 0; i < nr_dir_blks; i++) {
		RD_SECT(dir_inode->i_dev, dir_blk0_nr + i);
		pde = (struct dir_entry *)fsbuf;
		for (j = 0; j < SECTOR_SIZE / DIR_ENTRY_SIZE; j++,pde++) {
			if (memcmp(filename, pde->name, MAX_FILENAME_LEN) == 0){
				char* ch = pde->name;
				char* bf = buf;
				while(*bf){
					*ch++ = *bf++;		
				}
				*ch = 0;				
				WR_SECT(dir_inode->i_dev, dir_blk0_nr + i);
				return 0;			
			}
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries) /* all entries have been iterated */
			break;
	}
	return -1;
	
	
}
