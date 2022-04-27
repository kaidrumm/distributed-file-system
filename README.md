# Programming Assignment 4: Distributed File System
Kai Drumm kedr1998

## Portability notes:
This program was developed on MacOS, I hope that it is fully portable to your testing machine.

(We were not given operating system requirements for the project).

I had some difficulty installing the openssl.md5 library on MacOS. This is why I used:
```
import #include "/usr/local/include/node/openssl/md5.h"
```
instead of: 
```
#include <openssl.md5>
```
I do not know how to make this universally portable. I did try several ways of downloading openssl md5, and I do not know which one was successful for placing that library in the noted location.

## Folder Structure
* Each client needs its own home folder (**client1**, **client2**). The dfc.conf files should be there. This should contain all the files that you wish to send to the DFS system.
* **clientx/received/** is the location that GET commands will download files to.
* **clientx/logs/clientlog.txt** and **DFSX/logs/** provide debugging output.
* All received files and logfiles are cleared with **make clean**

## Implementation notes
* Configuration files dfc.conf and dfs.conf are read at the beginning of the server and client routines and are stored in global variables. Changing them in the middle of the program running will not cause password to update in program memory.
* XOR encryption is used on file contents. Passwords are stored in plain text.
* The client expects commands in lowercase. Directory arguments do not need a trailing slash.
* The client can only list up to 64 files
* All filenames and directory name combinations must be less than 256 characters
* The client is not multithreaded. Please do not run multiple clients with the same username!
* Traffic optimization is implemented (dfc.c lines 387-393)

## Commands to run the program:
```console
make re
./dfs /DFS1 10001 & ./dfs /DFS2 10002 & ./dfs /DFS3 10003 & ./dfs /DFS4 10004
./dfc dfc1.conf client1
./dfc dfc2.conf client2
```