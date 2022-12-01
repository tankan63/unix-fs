#define FUSE_USE_VERSION 27
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fuse.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#include "fs5600.h"

// Defining some systemcalls here
#define stat(a,b) error do not use stat()
#define open(a,b) error do not use open()
#define read(a,b,c) error do not use read()
#define write(a,b,c) error do not use write()


// === block manipulation functions ===

/* disk access.
 * All access is in terms of 4KB blocks; read and
 * write functions return 0 (success) or -EIO.
 *
 * read/write "nblks" blocks of data
 *   starting from block id "lba"
 *   to/from memory "buf".
 *     (see implementations in misc.c)
 */
extern int block_read(void *buf, int lba, int nblks);
extern int block_write(void *buf, int lba, int nblks);

/* bitmap functions
 */
void bit_set(unsigned char *map, int i)
{
    map[i/8] |= (1 << (i%8));
}
void bit_clear(unsigned char *map, int i)
{
    map[i/8] &= ~(1 << (i%8));
}
int bit_test(unsigned char *map, int i)
{
    return map[i/8] & (1 << (i%8));
}


//all starting variables and helper functions described here
super_t* super_block;
unsigned char* bitmap;
inode_t* root_inode;
dirent_t* root_dir;

char** getPathv();
int getPathc(char* path, char** pathv);
int truncatePath(const char* path, char** truncated_path);
int fs_truncate(const char* path, off_t len);

/*
 * Allocate a free block from the disk.
 *
 * success - return free block number
 * no free block - return -ENOSPC
 *
 * hint:
 *   - bit_set/bit_test might be useful.
 */
int alloc_blk() {
    /* Your code here */

	for (int i = 2; i < super_block->disk_size; i++) {
		if (bit_test(bitmap, i) == 0) {
			bit_set(bitmap, i);
			return i;
		}
	}
	
    return -ENOSPC;
}

/*
 * Return a block to disk, which can be used later.
 *
 * hint:
 *   - bit_clear might be useful.
 */
void free_blk(int i) {
    /* your code here*/


}


int find_free_inum() {
	int inode_cap = FS_BLOCK_SIZE * 8;
	for (int i = 2; i < inode_cap; i++) {
		if(!bit_test(bitmap, i)) {
			return i;
		}
	}
	return -ENOSPC;
}

// === FS helper functions ===


/* Two notes on path translation:
 *
 * (1) translation errors:
 *
 *   In addition to the method-specific errors listed below, almost
 *   every method can return one of the following errors if it fails to
 *   locate a file or directory corresponding to a specified path.
 *
 *   ENOENT - a component of the path doesn't exist.
 *   ENOTDIR - an intermediate component of the path (e.g. 'b' in
 *             /a/b/c) is not a directory
 *
 * (2) note on splitting the 'path' variable:
 *
 *   the value passed in by the FUSE framework is declared as 'const',
 *   which means you can't modify it. The standard mechanisms for
 *   splitting strings in C (strtok, strsep) modify the string in place,
 *   so you have to copy the string and then free the copy when you're
 *   done. One way of doing this:
 *
 *      char *_path = strdup(path);
 *      int inum = ... // translate _path to inode number
 *      free(_path);
 */


/* EXERCISE 2:
 * convert path into inode number.
 *
 * how?
 *  - first split the path into directory and file names
 *  - then, start from the root inode (which inode/block number is that?)
 *  - then, walk the dirs to find the final file or dir.
 *    when walking:
 *      -- how do I know if an inode is a dir or file? (hint: mode)
 *      -- what should I do if something goes wrong? (hint: read the above note about errors)
 *      -- how many dir entries in one inode? (hint: read Lab4 instructions about directory inode)
 *
 * hints:
 *  - you can safely assume the max depth of nested dirs is 10
 *  - a bunch of string functions may be useful (e.g., "strtok", "strsep", "strcmp")
 *  - "block_read" may be useful.
 *  - "S_ISDIR" may be useful. (what is this? read Lab4 instructions or "man inode")
 *
 * programing hints:
 *  - there are several functionalities that you will reuse; it's better to
 *  implement them in other functions.
 */


char** getPathv() {
	//this can hold upto 10 dir names
	char** pathv = (char**)malloc(20 * sizeof(char*));
	for (int i = 0; i < 10; i++) {
		//this can hold upto 27 + 1 characters for dir/file names
		pathv[i] = (char*)malloc(48 * sizeof(char*));
	}

	return pathv;
}

int getPathc(char* path, char** pathv) {
/*
	int i = 0;
	int max_depth = 10;
	char* token = strtok(path, "/");
	for (; i < max_depth; i++) {
		if (token == NULL) {
			break;
		}
		memcpy(pathv[i], token, strlen(token));
		token = strtok(NULL, "/");
	} 
	

	return i;
*/

	int i = 0;
	for(; i < 10; i++) {
		if ((pathv[i] = strtok((char*)path, "/")) == NULL) {
			break;
		}
		if (strlen(pathv[i]) > 27) {
			pathv[i][27] = '\0';
		}
		path = NULL;
	}
	return i;
}


int path2inum(const char *path) {
    /* your code here */

	char* ourpath = strdup(path);
	int inum = 2;
	if (strcmp(ourpath, "/") == 0) {
		free(ourpath);
		return inum;
	}
	char* token = strtok(ourpath, "/");
	int err_flag = 0;
	//printf("%s", ourpath);
	while (token != NULL) {
		inode_t* block = (inode_t*)malloc(FS_BLOCK_SIZE);
		int ret = block_read(block, inum, 1);

		if (ret < 0) {
			return -EIO;
		}

		if (!S_ISDIR(block->mode)) {
			return -ENOTDIR;
		}


		if (S_ISDIR(block->mode)) {
			dirent_t* dir = (dirent_t*)malloc(FS_BLOCK_SIZE);
			int ret = block_read(dir, block->ptrs[0], 1);

			if (ret < 0) {
				return -EIO;
			}

			int flag = 0;
			for (int i = 0; i < 128; i++) {
				if (dir[i].valid == 1 && strcmp(dir[i].name, token) == 0 ) {
					inum = dir[i].inode;
					flag = 1;
				}
			}

			if (flag == 0) {
				err_flag = -ENOENT;
				break;
			}
		}
		token = strtok(NULL, "/");
	}
	free(ourpath);
	//free(dir);
	
	if (err_flag != 0) {
		return -ENOENT;
	}
		
	return inum;
}


/* EXERCISE 2:
 * Helper function:
 *   copy the information in an inode to struct stat
 *   (see its definition below, and the "full definition" in 'man lstat'.)
 *
 *  struct stat {
 *        ino_t     st_ino;         // Inode number
 *        mode_t    st_mode;        // File type and mode
 *        nlink_t   st_nlink;       // Number of hard links
 *        uid_t     st_uid;         // User ID of owner
 *        gid_t     st_gid;         // Group ID of owner
 *        off_t     st_size;        // Total size, in bytes
 *        blkcnt_t  st_blocks;      // Number of blocks allocated
 *                                  // (note: block size is FS_BLOCK_SIZE;
 *                                  // and this number is an int which should be round up)
 *
 *        struct timespec st_atim;  // Time of last access
 *        struct timespec st_mtim;  // Time of last modification
 *        struct timespec st_ctim;  // Time of last status change
 *    };
 *
 *  [hints:
 *
 *    - read fs_inode in fs5600.h and compare with struct stat.
 *
 *    - you can safely ignore the types of attributes (e.g., "nlink_t",
 *      "uid_t", "struct timespec"), treat them as "unit32_t".
 *
 *    - the above "struct stat" does not show all attributes, but we don't care
 *      the rest attributes.
 *
 *    - for several fields in 'struct stat' there is no corresponding
 *    information in our file system:
 *      -- st_nlink - always set it to 1  (recall that fs5600 doesn't support links)
 *      -- st_atime - set to same value as st_mtime
 *  ]
 */

void inode2stat(struct stat *sb, struct fs_inode *in, uint32_t inode_num)
{
    memset(sb, 0, sizeof(*sb));
    sb->st_ino = inode_num;
    sb->st_mode = in->mode;
    sb->st_nlink = 1;
    sb->st_uid = in->uid;
    sb->st_gid = in->gid;
    sb->st_size = in->size;
    sb->st_blocks = (int)(in->size / FS_BLOCK_SIZE);
    sb->st_atime = in->mtime;
    sb->st_mtime = in->mtime;
    sb->st_ctime = in->ctime; 

    /* your code here */
}




// ====== FUSE APIs ========

/* EXERCISE 1:
 * init - this is called once by the FUSE framework at startup.
 *
 * The function should:
 *   - read superblock
 *   - check if the magic number matches FS_MAGIC
 *   - initialize whatever in-memory data structure your fs5600 needs
 *     (you may come back later when requiring new data structures)
 *
 * notes:
 *   - ignore the 'conn' argument.
 *   - use "block_read" to read data (if you don't know how it works, read its
 *     implementation in misc.c)
 *   - if there is an error, exit(1)
 *
 *
 */


void* fs_init(struct fuse_conn_info *conn)
{
    /* your code here */
	super_block = (super_t*)malloc(FS_BLOCK_SIZE);
	block_read(super_block, 0, 1);

	if (super_block->magic != FS_MAGIC) {
		exit(0);
	}

	bitmap = (unsigned char*)malloc(FS_BLOCK_SIZE);
	block_read(bitmap, 1, 1);

	root_inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	block_read(root_inode, 2, 1);

	root_dir = (dirent_t*)malloc(FS_BLOCK_SIZE);
	root_dir->valid = 1; //we start from here so it has to be valid
	root_dir->inode = 2; //root directory is always inode 2
	strcpy(root_dir->name, "/"); //assignment op error here
	

    return NULL;
}


/* EXERCISE 1:
 * statfs - get file system statistics
 * see 'man 2 statfs' for description of 'struct statvfs'.
 * Errors - none. Needs to work.
 */
int fs_statfs(const char *path, struct statvfs *st)
{
    /* needs to return the following fields (ignore others):
     *   [DONE] f_bsize = FS_BLOCK_SIZE
     *   [DONE] f_namemax = <whatever your max namelength is>
     *   [TODO] f_blocks = total image - (superblock + block map)
     *   [TODO] f_bfree = f_blocks - blocks used
     *   [TODO] f_bavail = f_bfree
     *
     * it's okay to calculate this dynamically on the rare occasions
     * when this function is called.
     */

    st->f_bsize = FS_BLOCK_SIZE;
    st->f_namemax = 27;  // why? see fs5600.h

    /* your code here */
    uint32_t free_blks = 0;
    for (int i = 2; i < super_block->disk_size; i++) {
    	if (bit_test(bitmap, i) == 0) {
		free_blks++;
	}
    }
    //total disk size minus the superblock and blockmap
    st->f_blocks = super_block->disk_size - 2;
    st->f_bfree = free_blks;
    st->f_bavail = st->f_bfree;
    return 0;
    //return -EOPNOTSUPP;
}


/* EXERCISE 2:
 * getattr - get file or directory attributes. For a description of
 *  the fields in 'struct stat', read 'man 2 stat'.
 *
 * You should:
 *  1. parse the path given by "const char * path",
 *     find the inode of the specified file,
 *       [note: you should implement the helfer function "path2inum"
 *       and use it.]
 *  2. copy inode's information to "struct stat",
 *       [note: you should implement the helper function "inode2stat"
 *       and use it.]
 *  3. and return:
 *     ** success - return 0
 *     ** errors - path translation, ENOENT
 */


int fs_getattr(const char *path, struct stat *sb)
{
    /* your code here */
	int inum = path2inum(path);
	if (inum < 0) {
		return inum;
	}	
	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	int ret = block_read(inode, inum, 1);

	if (ret < 0) {
		return -EIO;
	}

	inode2stat(sb, inode, inum);
	return 0;
    //return -EOPNOTSUPP;
}

/* EXERCISE 2:
 * readdir - get directory contents.
 *
 * call the 'filler' function for *each valid entry* in the
 * directory, as follows:
 *     filler(ptr, <name>, <statbuf>, 0)
 * where
 *   ** "ptr" is the second argument
 *   ** <name> is the name of the file/dir (the name in the direntry)
 *   ** <statbuf> is a pointer to the struct stat (of the file/dir)
 *
 * success - return 0
 * errors - path resolution, ENOTDIR, ENOENT
 *
 * hints:
 *   - this process is similar to the fs_getattr:
 *     -- you will walk file system to find the dir pointed by "path",
 *     -- then you need to call "filler" for each of
 *        the *valid* entry in this dir
 */
int fs_readdir(const char *path, void *ptr, fuse_fill_dir_t filler,
               off_t offset, struct fuse_file_info *fi)
{
    /* your code here */
	int inum = path2inum(path);
	if (inum == -ENOENT || inum == -ENOTDIR ) {
		return inum;
	}
	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	int ret = block_read(inode, inum, 1);

	if (ret < 0) {
		return -ENOENT;
	}

	if (!S_ISDIR(inode->mode)) {
		return -ENOTDIR;
	}	
	

	int blknum = inode->ptrs[0];
	// Maximum sirectory name lentgth
	dirent_t dir[FS_BLOCK_SIZE];
	block_read(dir, blknum, 1);

	for (int i = 0; i < 128; i++) {
		if (dir[i].valid) {
			struct stat sb;
			inode_t* in = (inode_t*)malloc(FS_BLOCK_SIZE);
			block_read(in, dir[i].inode, 1);
			inode2stat(&sb, in, dir->inode);
			filler(ptr, dir[i].name, &sb, 0);
		}	
	
	}


	return 0;

    //return -EOPNOTSUPP;
}


/* EXERCISE 3:
 * read - read data from an open file.
 * success: should return exactly the number of bytes requested, except:
 *   - if offset >= file len, return 0
 *   - if offset+len > file len, return #bytes from offset to end
 *   - on error, return <0
 * Errors - path resolution, ENOENT, EISDIR
 */
int fs_read(const char *path, char *buf, size_t len, off_t offset,
        struct fuse_file_info *fi)
{
    /* your code here */
	int bytes = 0;
	int inum  = path2inum(path);

	if (inum < 0) {
		return inum;
	}	

	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	int ret = block_read(inode, inum, 1);

	//char** pathv = getPathv();
	//int pathc = getPathc(path, pathv);

	if (ret < 0) {
		return -EIO;
	}

	if (S_ISDIR(inode->mode)) {
		return -EISDIR;
	}

	int total_size = inode->size;
	if (offset >= total_size) {
		return 0;
	}

	if (offset + len > total_size) {
		len = total_size - offset;
	}

	for (int i = 0; i < (FS_BLOCK_SIZE/4 - 5); i++) {

		if (offset/FS_BLOCK_SIZE > 0) {
			offset -= FS_BLOCK_SIZE;
			total_size -= FS_BLOCK_SIZE;
		}
	
		else {

		char* data = (char*)malloc(FS_BLOCK_SIZE);
		block_read(data, inode->ptrs[i], 1);

			if (offset + len < FS_BLOCK_SIZE) {
				memcpy(buf, data + offset, len);
				bytes += len;
				len = 0;
				break;
			}	

			else {
				//size_t diff = (FS_BLOCK_SIZE - offset);
				memcpy(buf, data + offset, FS_BLOCK_SIZE - offset);
				buf += FS_BLOCK_SIZE - offset;
				len -= FS_BLOCK_SIZE - offset;
				total_size -= FS_BLOCK_SIZE - offset;
				bytes += FS_BLOCK_SIZE - offset;
				offset = 0;
			
			
			}
		}
	
	}
	
	if (len <= 0) {
		return bytes;
	}

	return -ENOENT;	
}

/*
 * Helper function to get the directory of the path
 * Same logic as path2inum, just get the direcotry.
 */

char* fileName(char* path);



char* fileName(char* path) {
	int i = strlen(path) - 1;
	for (; i >= 0; i--) {
		if (path[i] == '/') {
			i++;
			break;
		}
	}
	char* fileName = &path[i];
	return fileName;
}

/* EXERCISE 3:
 * rename - rename a file or directory
 * success - return 0
 * Errors - path resolution, ENOENT, EINVAL, EEXIST
 *
 * ENOENT - source does not exist
 * EEXIST - destination already exists
 * EINVAL - source and destination are not in the same directory
 *
 * Note that this is a simplified version of the UNIX rename
 * functionality - see 'man 2 rename' for full semantics. In
 * particular, the full version can move across directories, replace a
 * destination file, and replace an empty directory with a full one.
 */
int fs_rename(const char *src_path, const char *dst_path)
{
    /* your code here */

	//printf("%d %d %d", -ENOENT, -EINVAL, -EEXIST);
	
	char* src = strdup(src_path);
	char* dest = strdup(dst_path);

	int s_inum = path2inum(src_path);
	int d_inum = path2inum(dst_path);

	/*
	char** s_pathv = getPathv();
	char** d_pathv = getPathv();
	int s_pathc = getPathc(src, s_pathv);
	int d_pathc = getPathc(dest, d_pathv);
	*/

	char* s_name = fileName(src);
	char* d_name = fileName(dest);
	//printf("\nfilename: %s\n", s_name);

	if (s_inum < 0) {
		return -ENOENT;
	}

	if (d_inum > 0) {
		return -EEXIST;
	}	

	char* psrc = strrchr(src, '/');
	char* pdest = strrchr(dest, '/');

	*psrc = '\0';
	*pdest = '\0';
	
	if (strcmp(src, dest) != 0) {
		return -EINVAL;
	}

	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);	
	int p_inum = path2inum(src);
	block_read(inode, p_inum, 1);
	dirent_t* buf = (dirent_t*)malloc(FS_BLOCK_SIZE);
	block_read(buf, inode->ptrs[0], 1);
	//dirent_t* dir2 = getDir(dest);
	
	for (int i = 0; i < 128; i++) {
		if (buf[i].valid == 1 && strcmp(buf[i].name, s_name) == 0) {
			strcpy(buf[i].name, d_name);
			block_write(buf, inode->ptrs[0], 1);
		}
	}	

	//strcpy(src_block[ent].name, d_pathv[d_pathc - 1]);
	//block_write(dir, src_block->inode, 1);

	//free(src_block);
	free(src);
	free(dest);
	//free(s_pathv);
	//free(d_pathv);
	return 0;
}

/* EXERCISE 3:
 * chmod - change file permissions
 *
 * success - return 0
 * Errors - path resolution, ENOENT.
 *
 * hints:
 *   - You can safely assume the "mode" is valid.
 *   - notice that "mode" only contains permissions
 *     (blindly assign it to inode mode doesn't work;
 *      why? check out Lab4 instructions about mode)
 *   - S_IFMT might be useful.
 */
int fs_chmod(const char *path, mode_t mode)
{
    /* your code here */

	int inum = path2inum(path);
	if (inum < 0) {
		return -ENOENT;
	}

	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	block_read(inode, inum, 1);

	inode->mode = inode->mode - (inode->mode & ~S_IFMT) +  (mode & ~S_IFMT);
	//block_write(inode, inum, 1);
	//struct stat* sb = (struct stat*)malloc(sizeof(struct stat));
	//inode2stat(sb, inode, inum);
	block_write(inode, inum, 1);	

	return 0;
    //return -EOPNOTSUPP;
}


static int pathv2inum(char* pathv[], int pathc) {
	int inum = 2;
	for(int i = 0; i < pathc; i++) {
		inode_t* in = (inode_t*)malloc(FS_BLOCK_SIZE);
		block_read(in, inum, 1);
		if (!S_ISDIR(in->mode)) {
			return -ENOTDIR;
		}
		int blknum = in->ptrs[0];
		dirent_t des[128];
		block_read(des, blknum, 1);
		int entry = 0;
		for (int j = 0; j < 128; j++) {
			if (des[j].valid && strcmp(des[j].name, pathv[i]) == 0) {
				inum = des[j].inode;
				entry = 1;
				break;
			}
		}

		if (!entry) {
			return -ENOENT;
		}
	
	}
	return inum;
}

int find_free_dirent(inode_t* inode) {
	dirent_t dir[128];
	int blknum = inode->ptrs[0];
	block_read(dir, blknum, 1);
	int free_dir = -1;

	for (int i = 0; i < 128; i++) {
		if(!dir[i].valid) {
			free_dir = 1;
			break;
		}
	}
	return free_dir;
}

/* EXERCISE 4:
 * create - create a new file with specified permissions
 *
 * success - return 0
 * errors - path resolution, EEXIST
 *          in particular, for create("/a/b/c") to succeed,
 *          "/a/b" must exist, and "/a/b/c" must not.
 *
 * If a file or directory of this name already exists, return -EEXIST.
 * If there are already 128 entries in the directory (i.e. it's filled an
 * entire block), you are free to return -ENOSPC instead of expanding it.
 * If the name is too long (longer than 27 letters), return -EINVAL.
 *
 * notes:
 *   - that 'mode' only has the permission bits. You have to OR it with S_IFREG
 *     before setting the inode 'mode' field.
 *   - Ignore the third parameter.
 *   - you will have to implement the helper funciont "alloc_blk" first
 *   - when creating a file, remember to initialize the inode ptrs.
 */
int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    uint32_t cur_time = time(NULL);
    struct fuse_context *ctx = fuse_get_context();
    uint16_t uid = ctx->uid;
    uint16_t gid = ctx->gid;

    // get rid of compiling warnings; you should remove later
    (void) uid, (void) gid, (void) cur_time;

    /* your code here */

	int inum = path2inum(path);
	if (inum > 0) {
		return -EEXIST;
	}

	char* file_name = fileName(path);
	if (strlen(file_name) > 28) {
		return -EINVAL;
	}

	// find parent dir
	
	int blk = alloc_blk();
	//inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);

	char* fullpath = strdup(path);
	char* ptr = strrchr(fullpath, "/");
	
	*ptr = '\0';

//	printf("\npath: %s\n", fullpath);
//	printf("filename: %s\n", file_name);
//	printf("actual path: %s\n", path);

	char* pathv[27];
	int pathc = getPathc(fullpath, pathv);
	int inum_dir = pathv2inum(pathv, pathc-1);
	inum = pathv2inum(pathv, pathc);
		
	inode_t* parent = (inode_t*)malloc(FS_BLOCK_SIZE);
	block_read(parent, inum_dir, 1);
	if (!S_ISDIR(parent->mode)) {
		return -ENOTDIR;
	}
	int free_dirent = find_free_dirent(&parent);
	if (free_dirent < 0) {
		return -ENOSPC;
	}
	
	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	inode->ctime = time(NULL);
	inode->uid = uid;
	inode->gid = gid;
	inode->mode = inode->mode - (inode->mode | S_IFREG) + (mode | S_IFREG);
	block_write(inode, blk, 1);

	int free_inum = find_free_inum();
	if (free_inum < 0) {
		return -ENOSPC;
	}
	bit_set(bitmap, free_inum);
	block_write(bitmap, 1, 1);
	block_write(inode, free_inum, 1);

	char* pathName = pathv[pathc-1];
	dirent_t newDir = {
		.valid = 1,
		.inode = free_inum,
		.name = "",
	};

	memcpy(newDir.name, pathName, strlen(pathName));
	newDir.name[strlen(pathName)-1] = '\0';

	dirent_t finalDir[128];
	int blknum = parent->ptrs[0];
	block_read(finalDir, blknum, 1);
	memcpy(&finalDir[free_dirent], &newDir, sizeof(dirent_t));
	block_write(finalDir, blknum, 1);
	free(fullpath);

	/*
	block_read(parent, p_inum, 1);
	dirent_t* buf = (dirent_t*)malloc(FS_BLOCK_SIZE);
	block_read(buf, parent->ptrs[0], 1);

	int flag = 0;

	for (int i = 0; i < 128; i++) {
		if (buf[i].valid == 0) {
			strcpy(buf[i].name, file_name);
			buf[i].inode = blk;
			buf[i].valid = 1;
			block_write(buf, parent->ptrs[0], 1);
			flag = 1;
			break;
		}
	
	}

	if (flag == 0) {
		return -ENOSPC;
	}

	//block_read(inode, blk, 1);
	
	inode->ctime = time(NULL);
	inode->uid = uid;
	inode->gid = gid;
	inode->mode = inode->mode - (inode->mode | S_IFREG) + (mode | S_IFREG);
	block_write(inode, blk, 1);
	*/
	//block_write(parent, p_inum, 1);
	return 0;


    //return -EOPNOTSUPP;
}



/* EXERCISE 4:
 * mkdir - create a directory with the given mode.
 *
 * Note that 'mode' only has the permission bits. You
 * have to OR it with S_IFDIR before setting the inode 'mode' field.
 *
 * success - return 0
 * Errors - path resolution, EEXIST
 * Conditions for EEXIST are the same as for create.
 *
 * hint:
 *   - there is a lot of similaries between fs_mkdir and fs_create.
 *     you may want to reuse many parts (note: reuse is not copy-paste!)
 */
int fs_mkdir(const char *path, mode_t mode)
{
    uint32_t cur_time = time(NULL);
    struct fuse_context *ctx = fuse_get_context();
    uint16_t uid = ctx->uid;
    uint16_t gid = ctx->gid;

    // get rid of compiling warnings; you should remove later
    (void) uid, (void) gid, (void) cur_time;

    /* your code here */

    	mode = mode | S_IFDIR;
	if (!S_ISDIR(mode)) {
		return -ENOTDIR;
	}


	char** pathv = getPathv();
	char* pathName = strdup(path);
	int pathc = getPathc(pathName, pathv);
	int inum_dir = pathv2inum(pathv, pathc-1);

	int inum = path2inum(path);
	if (inum > 0) {
		return -EEXIST;
	}

	char* file_name = fileName(path);
	if (strlen(file_name) > 28) {
		return -EINVAL;
	}

	inode_t* parent = (inode_t*)malloc(FS_BLOCK_SIZE);
	block_read(parent, inum_dir, 1);

	int free_dirent_num = find_free_dirent(&parent);
	if(!S_ISDIR(parent->mode)) {
		return -ENOTDIR;
	}

	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	time_t time_raw;
	time(&time_raw);
	inode->ctime = time_raw;
	inode->mtime = time_raw;
	inode->uid = uid;
	inode->gid = gid;
	//inode->mode = inode->mode - (inode->mode | S_IFREG) + (mode | S_IFREG);
	inode->mode = mode;
	inode->size = 0;

	int free_inum = find_free_inum();
	bit_set(bitmap, free_inum);
	block_write(bitmap, 1, 1);
	int free_dirent = find_free_inum();
	bit_set(bitmap, free_dirent);
	block_write(bitmap, 1, 1);

	inode->ptrs[0] = free_dirent;
	int* free_block = (int*)calloc(FS_BLOCK_SIZE, sizeof(int));
	block_write(free_block, free_dirent, 1);
	block_write(inode, free_inum, 1);

	char* newName = pathv[pathc-1];
	dirent_t newDir = {
		.valid = 1,
		.inode = free_inum,
		.name = "",
	};
	memcpy(newDir.name, newName, strlen(newName));
	newDir.name[strlen(newName)-1] = '\0';

	dirent_t* dir = (dirent_t*)malloc(FS_BLOCK_SIZE);
	int blknum = parent->ptrs[0];
	block_read(dir, blknum, 1);
	memcpy(&dir[free_dirent_num], &newDir, sizeof(dirent_t));
	block_write(dir, blknum, 1);


	// find parent dir
	
	/*
	int blk = alloc_blk();
	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);

	char* fullpath = strdup(path);
	char* ptr = strrchr(fullpath, "/");
	
	*ptr = '\0';

	//printf("\npath: %s\n", fullpath);
	//printf("filename: %s\n", file_name);
	//printf("actual path: %s\n", path);
		
	inode_t* parent = (inode_t*)malloc(FS_BLOCK_SIZE);
	//int p_inum = path2inum(fullpath);
	int p_inum = 2;

	
	if (p_inum < 0) {
		return inum;
	}	

	block_read(parent, p_inum, 1);
	dirent_t* buf = (dirent_t*)malloc(FS_BLOCK_SIZE);
	block_read(buf, parent->ptrs[0], 1);

	int flag = 0;

	for (int i = 0; i < 128; i++) {
		if (buf[i].valid == 0) {
			strcpy(buf[i].name, file_name);
			buf[i].inode = blk;
			buf[i].valid = 1;
			block_write(buf, parent->ptrs[0], 1);
			flag = 1;
			break;
		}
	
	}

	if (flag == 0) {
		return -ENOSPC;
	}

	block_read(inode, blk, 1);
	inode->ctime = time(NULL);
	inode->uid = uid;
	inode->gid = gid;
	inode->mode = inode->mode - (inode->mode | S_IFREG) + (mode | S_IFREG);
	block_write(inode, blk, 1);

	//block_write(parent, p_inum, 1);
	*/
	return 0;


    //return -EOPNOTSUPP;
}


/* EXERCISE 5:
 * unlink - delete a file
 *  success - return 0
 *  errors - path resolution, ENOENT, EISDIR
 *
 * hint:
 *   - you will have to implement the helper funciont "free_blk" first
 *   - remember to delete all data blocks as well
 *   - remember to update "mtime"
 */
int fs_unlink(const char *path)
{
    /* your code here */

	int inum = path2inum(path);

	//printf("-ENOENT: %d\n, -EISDIR: %d\n, -EIO: %d\n", -ENOENT, -EISDIR, -EIO);

	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	if (inum < 0) {
		return -20;
	}

	block_read(inode, inum, 1);
	


    return -EOPNOTSUPP;
}

/* EXERCISE 5:
 * rmdir - remove a directory
 *  success - return 0
 *  Errors - path resolution, ENOENT, ENOTDIR, ENOTEMPTY
 *
 * hint:
 *   - fs_rmdir and fs_unlink have a lot in common; think of reuse the code
 */
int fs_rmdir(const char *path)
{
    /* your code here */

	int inum = path2inum(path);
	if (inum < 0) {
		return -20;
	}

	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	block_read(inode, inum, 1);

	if (!S_ISDIR(inode->mode)) return -ENOTDIR;

	dirent_t ents[128];
	block_read(ents, inode->ptrs[0], 1);

	for (int i = 0; i < 128; i++) {
		if (ents[i].valid) {
			return -ENOTEMPTY;
		}
	}

	// Get parent path
	

    return -EOPNOTSUPP;
}

/* EXERCISE 6:
 * write - write data to a file
 * success - return number of bytes written. (this will be the same as
 *           the number requested, or else it's an error)
 *
 * Errors - path resolution, ENOENT, EISDIR, ENOSPC
 *  return EINVAL if 'offset' is greater than current file length.
 *  (POSIX semantics support the creation of files with "holes" in them,
 *   but we don't)
 *  return ENOSPC when the data exceed the maximum size of a file.
 */
int fs_write(const char *path, const char *buf, size_t len,
         off_t offset, struct fuse_file_info *fi)
{
    /* your code here */


	int len_written = 0;
	//char* dpath = strdup(path);
	int inum = path2inum(path);

	if (inum == -ENOENT || inum == -ENOTDIR) {
		return inum;
	}

	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	block_read(inode, inum, 1);

	if (!S_ISREG(inode->mode)) {
		return EISDIR;
	}

	int file_len = inode->size;
	if (offset > file_len) {
		return EINVAL;
	}

	int to_write = len;
	const char* curr = buf;
	off_t curr_offset = offset;

	int block_alloc = (file_len + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
	while (to_write > 0) {
		int written = 0;
		int block_idx = curr_offset / FS_BLOCK_SIZE;
		int block_inum;
		int block_start = 0;
		
		if (block_alloc < block_idx + 1) {
			block_inum = alloc_blk();
			bit_set(bitmap, block_inum);
			//update_bitmap();
			block_write(bitmap, 1, 1);
			inode->ptrs[block_idx] = block_inum;	
		}

	}


    return -EOPNOTSUPP;
}

/* EXERCISE 6:
 * truncate - truncate file to exactly 'len' bytes
 * note that CS5600 fs only allows len=0, meaning discard all data in this file.
 *
 * success - return 0
 * Errors - path resolution, ENOENT, EISDIR, EINVAL
 *    return EINVAL if len > 0.
 */
int fs_truncate(const char *path, off_t len)
{
    /* you can cheat by only implementing this for the case of len==0,
     * and an error otherwise.
     */
    if (len != 0) {
        return -EINVAL;        /* invalid argument */
    }
	
    int inum = path2inum(path);
    printf("%d", inum);

	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
    	block_read(inode, inum, 1);

	if (S_ISDIR(inode->mode)) {
		return -EISDIR;
	}

	int alloc_blk = (inode->size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;	

	int i = 0;
	for (; i < alloc_blk; i++){
		int blk_num = inode->ptrs[i];
		char zero[FS_BLOCK_SIZE];
		memset(zero, 0, FS_BLOCK_SIZE);
		block_write(zero, blk_num, 1);
		bit_clear(bitmap, blk_num);
		inode->ptrs[i] = 0;
	}

	inode->size = len;
	//update bitmap also
	//struct stat* sb = (struct stat*)malloc(sizeof(struct stat));
	//inode2stat(sb, inode, inum);
	block_write(inode, inum, 1);
	block_write(bitmap, 1, 1);

    /* your code here */
    return 0;
}

/* EXERCISE 6:
 * Change file's last modification time.
 *
 * notes:
 *  - read "man 2 utime" to know more.
 *  - when "ut" is NULL, update the time to now (i.e., time(NULL))
 *  - you only need to use the "modtime" in "struct utimbuf" (ignore "actime")
 *    and you only have to update "mtime" in inode.
 *
 * success - return 0
 * Errors - path resolution, ENOENT
 */
int fs_utime(const char *path, struct utimbuf *ut)
{
    /* your code here */

	int inum = path2inum(path);
	if (inum < 0) {
		return inum;
	}
	inode_t* inode = (inode_t*)malloc(FS_BLOCK_SIZE);
	block_read(inode, inum, 1);

	if (ut == NULL) {
		inode->mtime = time(NULL);
	}
	else {
		inode->mtime = ut->modtime;
	}
	block_write(inode, inum, 1);
	//struct stat* sb = (struct stat*)malloc(sizeof(struct stat));
	//inode2stat(sb, inode, inum);
	//block_write(inode, inum, 1);
	return 0;

    //return -EOPNOTSUPP;

}



/* operations vector. Please don't rename it, or else you'll break things
 */
struct fuse_operations fs_ops = {
    .init = fs_init,            /* read-mostly operations */
    .statfs = fs_statfs,
    .getattr = fs_getattr,
    .readdir = fs_readdir,
    .read = fs_read,
    .rename = fs_rename,
    .chmod = fs_chmod,

    .create = fs_create,        /* write operations */
    .mkdir = fs_mkdir,
    .unlink = fs_unlink,
    .rmdir = fs_rmdir,
    .write = fs_write,
    .truncate = fs_truncate,
    .utime = fs_utime,
};

