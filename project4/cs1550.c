/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define	FUSE_USE_VERSION 26

#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Size of a disk block (in bytes, must be a power of 2)
#define	BLOCK_SIZE 512

// Size of the disk file (5 MB = 5*2^20 bytes)
#define DISK_SIZE 5242880

// Number of blocks that can fit into the disk minus the tracking bitmap
#define BLOCK_COUNT (DISK_SIZE / BLOCK_SIZE - ((DISK_SIZE - 1) / (8 * BLOCK_SIZE * BLOCK_SIZE) + 1))

// We'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

// How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

// The path to the disk file
static const char *DISK_PATH = ".disk";

// Most recently allocated block index
static long last_block = 0;

// The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	// How many files are in this directory.
				// Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE - sizeof(long))

struct cs1550_disk_block
{
	//The next disk block, if needed. This is the next pointer in the linked 
	//allocation list
	long nNextBlock;

	//And all the rest of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

/*
 * Open the disk file in binary read/update mode.
 */
static FILE* cs1550_open_disk(void)
{
  FILE *f = fopen(DISK_PATH, "r+b");
  // Check if disk file is readable
  if (!f) {
    fprintf(stderr, "disk file does not exist or is not readable\n");
    return NULL;
  }
  // Check disk file size
  if (fseek(f, 0, SEEK_END) || ftell(f) != DISK_SIZE) {
    fclose(f);
    fprintf(stderr, "disk file is not valid\n");
    return NULL;
  }
  return f;
}

/*
 * Close the opened disk file ignoring errors.
 */
static void cs1550_close_disk(FILE *f)
{
  if (fclose(f)) {
    fprintf(stderr, "failed to close disk file\n");
  }
}

/*
 * Find the index of the next free block.
 * Return -1 when all blocks are used and -2 on error.
 */
static long next_free_block(FILE *f)
{
  long i, curr_byte_loc = -1;
  char curr_byte;
  for (i = 0; i < BLOCK_COUNT; ++i) {
    last_block = (last_block + 1) % BLOCK_COUNT;
    // Ignore root directory block
    if (!last_block) continue;
    long byte_loc = DISK_SIZE - (last_block / 8) - 1;
    if (byte_loc != curr_byte_loc) {
      if (fseek(f, byte_loc, SEEK_SET) ||
          fread(&curr_byte, 1, 1, f) != 1) return -2;
      curr_byte_loc = byte_loc;
    }
    int bit_loc = last_block % 8;
    char mask = 1 << bit_loc;
    if (!(curr_byte & mask)) {
      return last_block;
    }
  }
  return -1;
}

/*
 * Request a number of free blocks and return their indices.
 * Return NULL when all blocks are used or on error.
 */
static long* request_free_blocks(FILE *f, size_t num)
{
  long *block_indices = malloc(sizeof(long) * num);
  *block_indices = 0;
  long saved_last_block = last_block;
  int i;
  for (i = 0; i < num; ++i) {
    long block_idx = next_free_block(f);
    if (block_idx < 0 || block_idx == *block_indices) {
      free(block_indices);
      last_block = saved_last_block;
      return NULL;
    }
    *(block_indices + i) = block_idx;
  }
  return block_indices;
}

/*
 * Set a bit in the tracking bitmap. 1 indicates that the block is being used,
 * and 0 indicates that the block is free. Return -1 on error.
 */
static int set_bitmap(FILE *f, long block_idx, char val)
{
  if (block_idx >= BLOCK_COUNT) {
    fprintf(stderr, "requested block %ld does not exist\n", block_idx);
    return -1;
  }
  long byte_loc = DISK_SIZE - (block_idx / 8) - 1;
  char byte;
  int bit_loc = block_idx % 8;
  if (fseek(f, byte_loc, SEEK_SET) ||
      fread(&byte, 1, 1, f) != 1) return -1;
  char mask = 1 << bit_loc;
  if (val) {
    byte |= mask;
  } else {
    byte &= (~mask);
  }
  if (fseek(f, byte_loc, SEEK_SET) ||
      fwrite(&byte, 1, 1, f) != 1) return -1;
  return 0;
}

/*
 * Load a block at some certain block index from disk.
 * This function does not assume the type of the block.
 */
static void* load_block(FILE *f, long block_idx)
{
  if (block_idx >= BLOCK_COUNT) {
    fprintf(stderr, "requested block %ld does not exist\n", block_idx);
    return NULL;
  }
  if (fseek(f, block_idx * BLOCK_SIZE, SEEK_SET)) {
    fprintf(stderr, "failed to seek to block %ld\n", block_idx);
    return NULL;
  }
  void *block = malloc(BLOCK_SIZE);
  if (fread(block, BLOCK_SIZE, 1, f) != 1) {
    fprintf(stderr, "failed to load block %ld\n", block_idx);
    free(block);
    return NULL;
  }
  return block;
}

/*
 * Save a block at some certain block index to disk.
 * This function does not assume the type of the block.
 * Return -1 on error.
 */
static int save_block(FILE *f, long block_idx, void *block)
{
  if (block_idx >= BLOCK_COUNT) {
    fprintf(stderr, "requested block %ld does not exist\n", block_idx);
    return -1;
  }
  if (fseek(f, block_idx * BLOCK_SIZE, SEEK_SET)) {
    fprintf(stderr, "failed to seek to block %ld\n", block_idx);
    return -1;
  }
  if (fwrite(block, BLOCK_SIZE, 1, f) != 1) {
    fprintf(stderr, "failed to write to block %ld\n", block_idx);
    return -1;
  }
  return 0;
}

/*
 * Load the root directory block from disk.
 */
static cs1550_root_directory* load_root_directory(FILE *f)
{
  return (cs1550_root_directory*) load_block(f, 0);
}

/*
 * Load a subdirectory block at some certain block index from disk.
 */
static cs1550_directory_entry* load_subdirectory(FILE *f, long block_idx)
{
  return (cs1550_directory_entry*) load_block(f, block_idx);
}

/*
 * Load a file block at some certain block index from disk.
 */
static cs1550_disk_block* load_file_block(FILE *f, long block_idx)
{
  return (cs1550_disk_block*) load_block(f, block_idx);
}

/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
   
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
    char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1],
         extension[MAX_EXTENSION + 1];
    int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
    if (count < 1) return -ENOENT;
    // Open disk file
    FILE *disk = cs1550_open_disk();
    if (!disk) return -ENXIO;
    // Load root directory block
    cs1550_root_directory* root = load_root_directory(disk);
    if (!root) {
      res = -EIO;
    } else {
      // Search for subdirectory
      size_t dir_idx;
      for (dir_idx = 0; dir_idx < root->nDirectories; ++dir_idx) {
        if (strcmp(root->directories[dir_idx].dname, directory) == 0) {
          break;
        }
      }
      if (dir_idx == root->nDirectories) {
        res = -ENOENT;
      } else {
        if (count == 1) {
          stbuf->st_mode = S_IFDIR | 0755;
          stbuf->st_nlink = 2;
          res = 0; //no error
        } else {
          // Load subdirectory
          cs1550_directory_entry* dir =
              load_subdirectory(disk, root->directories[dir_idx].nStartBlock);
          if (!dir) {
            res = -EIO;
          } else {
            // Search for file
            res = -ENOENT;
            size_t file_idx;
            for (file_idx = 0; file_idx < dir->nFiles; ++file_idx) {
              if (strcmp(dir->files[file_idx].fname, filename) == 0 &&
                  strcmp(dir->files[file_idx].fext, extension) == 0) {
                stbuf->st_mode = S_IFREG | 0666; 
                stbuf->st_nlink = 1; //file links
                stbuf->st_size = dir->files[file_idx].fsize; //file size
                res = 0; // no error
                break;
              }
            }
            free(dir);
          }
        }
      }
      free(root);
    }
    // Close disk file
    cs1550_close_disk(disk);
	}
	return res;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;

  char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1],
       extension[MAX_EXTENSION + 1];
  int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
  if (count > 1) return -ENOENT;
	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
  // Open disk file
  FILE *disk = cs1550_open_disk();
  if (!disk) return -ENXIO;
  printf("opened disk\n");
  int res = -ENOENT;
  // Load root directory block
  cs1550_root_directory* root = load_root_directory(disk);
  if (!root) {
    res = -EIO;
  } else {
    size_t dir_idx;
    if (!strcmp(path, "/")) {
      // Add subdirectories
      for (dir_idx = 0; dir_idx < root->nDirectories; ++dir_idx) {
        filler(buf, root->directories[dir_idx].dname, NULL, 0);
      }
      res = 0;
    } else if (count == 1) {
      // Search for subdirectory
      size_t dir_idx;
      for (dir_idx = 0; dir_idx < root->nDirectories; ++dir_idx) {
        if (strcmp(root->directories[dir_idx].dname, directory) == 0) {
          break;
        }
      }
      // If subdirectory exists
      if (dir_idx < root->nDirectories) {
        // Load subdirectory
        cs1550_directory_entry* dir =
            load_subdirectory(disk, root->directories[dir_idx].nStartBlock);
        if (!dir) {
          res = -EIO;
        } else {
          // Add files
          size_t file_idx;
          for (file_idx = 0; file_idx < dir->nFiles; ++file_idx) {
            // Get full file name (fname.ext)
            char filename[MAX_FILENAME + MAX_EXTENSION + 1];
            strcpy(filename, dir->files[file_idx].fname);
            strcat(filename, ".");
            strcat(filename, dir->files[file_idx].fext);
            filler(buf, filename, NULL, 0);
          }
          res = 0;
          free(dir);
        }
      }
    }
    free(root);
  }
  // Close disk file
  cs1550_close_disk(disk);
	return res;
}

/* 
 * Create a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) mode;
  // Check parts
  char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1],
       extension[MAX_EXTENSION + 1];
  int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
  if (count > 1) return -EPERM;
  // Check if the directory name is overlength
  if (strlen(directory) > MAX_FILENAME) return -ENAMETOOLONG;
  // Open disk file
  FILE *disk = cs1550_open_disk();
  if (!disk) return -ENXIO;
  int res = 0;
  // Load root directory block
  cs1550_root_directory* root = load_root_directory(disk);
  if (!root) {
    res = -ENXIO;
  } else {
    // Search for subdirectory
    size_t dir_idx;
    for (dir_idx = 0; dir_idx < root->nDirectories; ++dir_idx) {
      if (strcmp(root->directories[dir_idx].dname, directory) == 0) {
        res = -EEXIST;
        break;
      }
    }
    // When the directory is full
    if (!res && root->nDirectories == MAX_DIRS_IN_ROOT) {
      res = -ENOSPC;
    }
    // Subdirectory does not exist, create new
    if (!res) {
      long new_block_idx = next_free_block(disk);
      if (new_block_idx > 0) {
        strcpy(root->directories[root->nDirectories].dname, directory);
        root->directories[root->nDirectories].nStartBlock = new_block_idx;
        root->nDirectories++;
        if (save_block(disk, 0, root) || set_bitmap(disk, new_block_idx, 1)) {
          res = -EIO;
        }
      } else if (new_block_idx == -1) {
        res = -ENOSPC;
      } else {
        res = -EIO;
      }
    }
    free(root);
  }
  cs1550_close_disk(disk);
	return res;
}

/* 
 * Remove a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/* 
 * Do the actual creation of a file. Mode and dev can be ignored.
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
  // Check parts
  char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1],
       extension[MAX_EXTENSION + 1];
  int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
  if (count < 3) return -EPERM;
  // Check if the directory name is overlength
  if (strlen(directory) > MAX_FILENAME) return -ENOENT;
  // Check if the file name or directory name is overlength
  if (strlen(filename) > MAX_FILENAME ||
      strlen(extension) > MAX_EXTENSION) return -ENAMETOOLONG;
  // Open disk file
  FILE *disk = cs1550_open_disk();
  if (!disk) return -ENXIO;
  int res = -ENOENT;
  // Load root directory block
  cs1550_root_directory* root = load_root_directory(disk);
  if (!root) {
    res = -EIO;
  } else {
    // Search for subdirectory
    size_t dir_idx;
    for (dir_idx = 0; dir_idx < root->nDirectories; ++dir_idx) {
      if (strcmp(root->directories[dir_idx].dname, directory) == 0) {
        res = 0;
        break;
      }
    }
    // Subdirectory exists
    if (!res) {
      // Load subdirectory
      long dir_block_idx = root->directories[dir_idx].nStartBlock;
      cs1550_directory_entry* dir = load_subdirectory(disk, dir_block_idx);
      if (!dir) {
        res = -EIO;
      } else {
        // Search for file
        size_t file_idx;
        for (file_idx = 0; file_idx < dir->nFiles; ++file_idx) {
          if (strcmp(dir->files[file_idx].fname, filename) == 0 &&
              strcmp(dir->files[file_idx].fext, extension) == 0) {
            res = -EEXIST;
            break; 
          }
        }
        // When the directory is full
        if (!res && dir->nFiles == MAX_FILES_IN_DIR) {
          res = -ENOSPC;
        }
        if (!res) {
          // Create new file
          long new_block_idx = next_free_block(disk);
          if (new_block_idx > 0) {
            strcpy(dir->files[dir->nFiles].fname, filename);
            strcpy(dir->files[dir->nFiles].fext, extension);
            dir->files[dir->nFiles].nStartBlock = new_block_idx;
            dir->nFiles++;
            if (save_block(disk, dir_block_idx, dir) ||
                set_bitmap(disk, new_block_idx, 1)) {
              res = -EIO;
            }
          } else if (new_block_idx == -1) {
            res = -ENOSPC;
          } else {
            res = -EIO;
          }
        }
        free(dir);
      }
    }
    free(root);
  }
  cs1550_close_disk(disk);
	return res;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) fi;
	// Check that size is > 0
  if (!size) return -EPERM;
  // Check parts
  char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1],
       extension[MAX_EXTENSION + 1];
  int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
  if (count == EOF) return -EISDIR;
  // Check if the directory name, file name, or extension name is overlength
  if (strlen(directory) > MAX_FILENAME ||
      strlen(filename) > MAX_FILENAME ||
      strlen(extension) > MAX_EXTENSION) return -ENOENT;
  // Open disk file
  FILE *disk = cs1550_open_disk();
  if (!disk) return -ENXIO;
  int res = -ENOENT;
  // Load root directory block
  cs1550_root_directory* root = load_root_directory(disk);
  if (!root) {
    res = -EIO;
  } else {
    // Search for subdirectory
    size_t dir_idx;
    for (dir_idx = 0; dir_idx < root->nDirectories; ++dir_idx) {
      if (strcmp(root->directories[dir_idx].dname, directory) == 0) {
        res = 0;
        break;
      }
    }
    // Subdirectory exists
    if (!res && count == 1) {
      // Attempting to read existing directory
      res = -EISDIR;
    } else if (!res && count == 2) {
      // No extension name provided
      res = -ENOENT;
    } else if (!res) {
      res = -ENOENT;
      // Load subdirectory
      long dir_block_idx = root->directories[dir_idx].nStartBlock;
      cs1550_directory_entry* dir = load_subdirectory(disk, dir_block_idx);
      if (!dir) {
        res = -EIO;
      } else {
        // Search for file
        size_t file_idx;
        for (file_idx = 0; file_idx < dir->nFiles; ++file_idx) {
          if (strcmp(dir->files[file_idx].fname, filename) == 0 &&
              strcmp(dir->files[file_idx].fext, extension) == 0) {
            res = 0;
            break; 
          }
        }
        if (!res) {
          if (offset > dir->files[file_idx].fsize) {
            res = -EFBIG;
          } else {
            // Set read size bound
            if (offset + size > dir->files[file_idx].fsize) {
              size = dir->files[file_idx].fsize - offset;
            }
            // Load file block
            long block_idx = dir->files[file_idx].nStartBlock;
            cs1550_disk_block* file = load_file_block(disk, block_idx);
            // Skip to the block offset is at
            while (offset > MAX_DATA_IN_BLOCK) {
              if (!file || !file->nNextBlock) {
                res = -EIO;
                break;
              }
              block_idx = file->nNextBlock;
              free(file);
              file = load_file_block(disk, block_idx);
              offset -= MAX_DATA_IN_BLOCK;
            }
            if (!res) {
              // Reading file data
              size_t remaining = size;
              while (remaining && !res) {
                if (!file) {
                  res = -EIO;
                  break;
                }
                size_t read_size = MAX_DATA_IN_BLOCK - offset;
                if (remaining < read_size) {
                  read_size = remaining;
                }
                // Copy data from current block
                memcpy(buf, file->data + offset, read_size);
                remaining -= read_size;
                if (remaining && !res) {
                  block_idx = file->nNextBlock;
                  free(file);
                  file = load_file_block(disk, block_idx);
                  buf += read_size;
                  offset = 0; 
                }
              }
            }
            if (file) free(file);
          }
        }
        free(dir);
      }
    }
    free(root);
  }
  cs1550_close_disk(disk);
	return res ? res : size;
}

/* 
 * Write size bytes from buf into file starting from offset
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
			  off_t offset, struct fuse_file_info *fi)
{
	(void) fi;
	// Check that size is > 0
  if (!size) return -EPERM;
  size_t total_size = size + offset;
  // Check parts
  char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1],
       extension[MAX_EXTENSION + 1];
  int count = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
  if (count < 3) return -ENOENT;
  // Check if the directory name, file name, or extension name is overlength
  if (strlen(directory) > MAX_FILENAME ||
      strlen(filename) > MAX_FILENAME ||
      strlen(extension) > MAX_EXTENSION) return -ENOENT;
  // Open disk file
  FILE *disk = cs1550_open_disk();
  if (!disk) return -ENXIO;
  int res = -ENOENT;
  // Load root directory block
  cs1550_root_directory* root = load_root_directory(disk);
  if (!root) {
    res = -EIO;
  } else {
    // Search for subdirectory
    size_t dir_idx;
    for (dir_idx = 0; dir_idx < root->nDirectories; ++dir_idx) {
      if (strcmp(root->directories[dir_idx].dname, directory) == 0) {
        res = 0;
        break;
      }
    }
    // Subdirectory exists
    if (!res) {
      res = -ENOENT;
      // Load subdirectory
      long dir_block_idx = root->directories[dir_idx].nStartBlock;
      cs1550_directory_entry* dir = load_subdirectory(disk, dir_block_idx);
      if (!dir) {
        res = -EIO;
      } else {
        // Search for file
        size_t file_idx;
        for (file_idx = 0; file_idx < dir->nFiles; ++file_idx) {
          if (strcmp(dir->files[file_idx].fname, filename) == 0 &&
              strcmp(dir->files[file_idx].fext, extension) == 0) {
            res = 0;
            break; 
          }
        }
        if (!res) {
          if (offset > dir->files[file_idx].fsize) {
            res = -EFBIG;
          } else {
            size_t writable_file_size = dir->files[file_idx].fsize - offset;
            // Load file block
            long block_idx = dir->files[file_idx].nStartBlock;
            cs1550_disk_block* file = load_file_block(disk, block_idx);
            // Skip to the block offset is at
            while (offset > MAX_DATA_IN_BLOCK) {
              if (!file || !file->nNextBlock) {
                res = -EIO;
                break;
              }
              block_idx = file->nNextBlock;
              free(file);
              file = load_file_block(disk, block_idx);
              offset -= MAX_DATA_IN_BLOCK;
            }
            if (!res) {
              // Overriding file data
              size_t remaining = size;
              long* new_block_idxs = NULL;
              int used_new_blocks = 0;
              if (remaining - writable_file_size > MAX_DATA_IN_BLOCK - offset) {
                int require_block_num = (remaining - writable_file_size +
                                         offset - 1) / MAX_DATA_IN_BLOCK;
                new_block_idxs = request_free_blocks(disk, require_block_num);
                if (!new_block_idxs) res = -ENOSPC;
              }
              while (remaining && !res) {
                if (!file) {
                  res = -EIO;
                  break;
                }
                size_t write_size = MAX_DATA_IN_BLOCK - offset;
                if (remaining < write_size) {
                  write_size = remaining;
                }
                // Copy data to current block
                memcpy(file->data + offset, buf, write_size);
                remaining -= write_size;
                if (remaining && !file->nNextBlock) {
                  // Use next new block block for file appending
                  long next_block_idx = *(new_block_idxs + used_new_blocks);
                  if (set_bitmap(disk, next_block_idx, 1)) {
                    res = -EIO;
                    break;
                  }
                  file->nNextBlock = next_block_idx;
                  used_new_blocks++;
                }
                if (save_block(disk, block_idx, file)) {
                  res = -EIO;
                  break;
                }
                if (remaining) {
                  block_idx = file->nNextBlock;
                  free(file);
                  file = load_file_block(disk, block_idx);
                  buf += write_size;
                  offset = 0; 
                }
              }
              if (res) {
                int i;
                for (i = 0; i < used_new_blocks; ++i) {
                  set_bitmap(disk, *(new_block_idxs + i), 0);
                }
              }
              if (new_block_idxs) free(new_block_idxs);
            }
            if (file) free(file);
            if (!res && total_size > dir->files[file_idx].fsize) {
              dir->files[file_idx].fsize = total_size;
              if (save_block(disk, dir_block_idx, dir)) res = -EIO;
            }
          }
        }
        free(dir);
      }
    }
    free(root);
  }
  cs1550_close_disk(disk);
	return res ? res : size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
