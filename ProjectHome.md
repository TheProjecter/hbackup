This project is now hosted at [SourceForge](http://sourceforge.net/projects/hbackup/).

The storage is a bit similar to git's, using MD5 hashes (using openssl), hence the name.
It can use parsers to recognize a number of files/directories, that influence its behavior: CVS is ready, Subversion is in the works...
It can use ignore filters that can be set for any type of file (regular, dir, link, block/char device, FIFO, socket): on size (min/max), on name (start, end, regex) or on path (start, end, regex).
Backups can be local and remote (CIFS and NFSv3 supported so far).