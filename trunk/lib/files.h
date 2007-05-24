/*
     Copyright (C) 2006-2007  Herve Fache

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License version 2 as
     published by the Free Software Foundation.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

#ifndef FILES_H
#define FILES_H

#include <fstream>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <zlib.h>

namespace hbackup {

class File {
  string          _prefix;    // mount or share path or prrefix
  string          _path;      // file path
  string          _checksum;  // file checksum
  string          _link;      // what the link points to (if a link, of course)
  mode_t          _type;      // file type (0 if metadata not available)
  time_t          _mtime;     // time of last modification
  long long       _size;      // on-disk size, in bytes
  uid_t           _uid;       // user ID of owner
  gid_t           _gid;       // group ID of owner
  mode_t          _mode;      // permissions
  FILE*           _fd;        // file descriptor
  long long       _dsize;     // uncompressed data size, in bytes
  bool            _fwrite;    // file open for write
  unsigned char*  _fbuffer;   // buffer for file compression during read/write
  bool            _fempty;    // compression buffer not empty
  bool            _feof;      // All data read AND decompressed
  EVP_MD_CTX*     _ctx;       // openssl resources
  z_stream*       _strm;      // zlib resources
  // Convert MD5 to readable string
  static void md5sum(
    string&               checksum_out,
    const unsigned char*  checksum_in,
    int                   bytes);
public:
  // Max buffer size for read/write
  static const size_t chunk = 409600;
  // Constructor for existing file (if only one argument, it will be the path)
  File(const string& access_path, const string& path = "");
  // Constructor for given file data
  File(
    const string& prefix,
    const string& path,
    const string& link,
    mode_t        type,
    time_t        mtime,
    long long     size,
    uid_t         uid,
    gid_t         gid,
    mode_t        mode) :
      _prefix(prefix),
      _path(path),
      _checksum(""),
      _link(link),
      _type(type),
      _mtime(mtime),
      _size(size),
      _uid(uid),
      _gid(gid),
      _mode(mode),
      _fd(NULL) {}
  // Constructor for given line
  File(char* line, size_t size);
  // Need '<' to sort list
  bool operator<(const File&) const;
  bool operator!=(const File&) const;
  bool operator==(const File& right) const { return ! (*this != right); }
  bool metadiffer(const File&) const;
  string name() const;
  string prefix() const { return _prefix; };
  string path() const { return _path; };
  string checksum() const { return _checksum; }
  string fullPath(int max_size = -1) const;
  mode_t type() const { return _type; }
  time_t mtime() const { return _mtime; };
  long long size() const { return _size; };
  long long dsize() const { return _dsize; };
  bool eof() const { return _feof; };
  // Line containing all data (argument for debug only)
  string line(bool nodates = false) const;
  void setPrefix(const string& prefix) { _prefix = prefix; }
  void setPath(const string& path) { _path = path; }

  // Open file, for read or write (no append), with or without compression
  int open(const char* req_mode, unsigned int compression = 0);
  // Close file, for read or write (no append), with or without compression
  int close();
  // Read file sets eof (check with eof()) when all data is read and ready
  ssize_t read(unsigned char* buffer, size_t count);
  // Write to file (signal end of file for compression end)
  ssize_t write(unsigned char* buffer, size_t count, bool eof);
  // Get a line of data
  ssize_t readLine(unsigned char* buffer, size_t count, char delim);
  // Add a line of data
  ssize_t writeLine(const unsigned char* buffer, size_t count, char delim);
  // Get parameters from one line of data
  ssize_t readParams(vector<string>& params);

  // Test whether dir exists, create it if requested
  static int testDir(const string& path, bool create);
  // Test whether file exists, create it if requested
  static int testReg(const string& path, bool create);
  // Transform letter into mode
  static char typeLetter(mode_t mode);
  // Transform mode into letter
  static mode_t typeMode(char letter);
  // Copy, compress and compute checksum (MD5), all in once
  static int zcopy(
    const string& source_path,
    const string& dest_path,
    long long*    size_in,
    long long*    size_out,
    string*       checksum_in,
    string*       checksum_out,
    int           compress);
  // Compute file checksum (MD5)
  static int getChecksum(const string& path, string& checksum);
  // Read parameters from line
  static int decodeLine(const string& line, vector<string>& params);
};

}

#endif
