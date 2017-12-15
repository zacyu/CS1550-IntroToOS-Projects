# CS 1550 – Project 4: File Systems

## Project Description
FUSE (http://fuse.sourceforge.net/) is a Linux kernel extension that allows for a user space program to provide the implementations for the various file-related syscalls.  We will be using FUSE to create our own file system, managed via a single file that represents our disk device.  Through FUSE and our implementation, it will be possible to interact with our newly created file system using standard UNIX/Linux programs in a transparent way.

From a user interface perspective, our file system will be a two level directory system, with the following restrictions/simplifications:

1. The root directory “\\” will only contain other subdirectories, and no regular files
2. The subdirectories will only contain regular files, and no subdirectories of their own
3. All files will be full access (i.e., chmod 0666), with permissions to be mainly ignored
4. Many file attributes such as creation and modification times will not be accurately stored
5. Files cannot be truncated
From an implementation perspective, the file system will keep data on “disk” via a linked list allocation strategy, outlined below.

## Full Project Description
https://people.cs.pitt.edu/~jmisurda/teaching/cs1550/2181/cs1550-2181-project4.htm

## Demo
![Demo](demo.gif)
