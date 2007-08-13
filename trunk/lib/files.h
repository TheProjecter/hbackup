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
#include <list>

using namespace std;

#include <openssl/md5.h>
#include <openssl/evp.h>
#include <zlib.h>

namespace hbackup {

class Node {
protected:
  char*     _path;      // file path
  char*     _name;      // file name
  char      _type;      // file type (0 if metadata not available)
  time_t    _mtime;     // time of last modification
  long long _size;      // on-disk size, in bytes
  uid_t     _uid;       // user ID of owner
  gid_t     _gid;       // group ID of owner
  mode_t    _mode;      // permissions
  bool      _parsed;    // more info available using proper type
  void  metadata(const char* path);
  char* basename(char* path) {
    char* name = strrchr(path, '/');
    if (name != NULL) {
      name++;
    } else {
      name = path;
    }
    return name;
  }
public:
  // Default constructor
  Node(const Node& g) :
        _path(NULL),
        _name(NULL),
        _type(g._type),
        _mtime(g._mtime),
        _size(g._size),
        _uid(g._uid),
        _gid(g._gid),
        _mode(g._mode),
        _parsed(false) {
    asprintf(&_path, "%s", g._path);
    _name = basename(_path);
  }
  // Constructor for path in the VFS
  Node(const char *path, const char* name = "");
  // Constructor for given file metadata
  Node(
      const char* path,
      char        type,
      time_t      mtime,
      long long   size,
      uid_t       uid,
      gid_t       gid,
      mode_t      mode) :
        _path(NULL),
        _name(NULL),
        _type(type),
        _mtime(mtime),
        _size(size),
        _uid(uid),
        _gid(gid),
        _mode(mode),
        _parsed(false) {
    asprintf(&_path, "%s", path);
    _name = basename(_path);
  }
  virtual ~Node() {
    free(_path);
  }
  // Operators
  virtual bool  operator<(const Node&)  const;
  virtual bool  operator!=(const Node&) const;
  // Data read access
  virtual bool  isValid() const { return _type != '?'; }
  const char*   path(int offset = 0) const {
    return &_path[offset];
  }
  const char*   name()    const { return _name;   }
  char          type()    const { return _type;   }
  time_t        mtime()   const { return _mtime;  }
  long long     size()    const { return _size;   }
  uid_t         uid()     const { return _uid;    }
  gid_t         gid()     const { return _gid;    }
  mode_t        mode()    const { return _mode;   }
  bool          parsed()  const { return _parsed; }
};

class File2 : public Node {
protected:
  char*     _checksum;
public:
  // Constructor for existing Node
  File2(const Node& g) :
      Node(g),
      _checksum(NULL) {
    _parsed = true;
  }
  // Constructor for path in the VFS
  File2(const char *path, const char* name = "") :
      Node(path, name),
      _checksum(NULL) {
    _parsed = true;
  }
  // Constructor for given file metadata
  File2(
    const char* name,
    char        type,
    time_t      mtime,
    long long   size,
    uid_t       uid,
    gid_t       gid,
    mode_t      mode,
    const char* checksum) :
      Node(name, type, mtime, size, uid, gid, mode),
      _checksum(NULL) {
    _parsed = true;
    asprintf(&_checksum, "%s", checksum);
  }
  ~File2() {
    free(_checksum);
  }
  // Create empty file
  int create();
  bool        isValid() const { return _type == 'f'; }
  // Data read access
  const char* checksum() const { return _checksum;  }
};

class Directory : public Node {
  list<Node*> _nodes;
public:
  // Constructor for existing Node
  Directory(const Node& g) :
      Node(g) {
    _size  = 0;
    _mtime = 0;
    _parsed = true;
    _nodes.clear();
  }
  // Constructor for path in the VFS
  Directory(const char *path, const char* name = "") :
      Node(path, name) {
    _size  = 0;
    _mtime = 0;
    _parsed = true;
    _nodes.clear();
  }
  ~Directory() {
    deleteList();
  }
  // Create directory
  int   create();
  // Create list of Nodes contained in directory
  int   createList();
  void  deleteList();
  bool  isValid() const                     { return _type == 'd'; }
  list<Node*>& nodesList()                  { return _nodes; }
  const list<Node*>& nodesListConst() const { return _nodes; }
};

class Link : public Node {
  char*     _link;
public:
  // Constructor for existing Node
  Link(const Node& g) :
      Node(g),
      _link(NULL) {
    _mtime = 0;
    _parsed = true;
    char* link = (char*) malloc(FILENAME_MAX + 1);
    int   size = readlink(_path, link, FILENAME_MAX);

    if (size < 0) {
      _type = '?';
      free(link);
    } else {
      _link = (char*) malloc(size + 1);
      strncpy(_link, link, size);
      _link[size] = '\0';
    }
  }
  // Constructor for given file metadata
  Link(
    const char* name,
    char        type,
    time_t      mtime,
    long long   size,
    uid_t       uid,
    gid_t       gid,
    mode_t      mode,
    const char* link) :
        Node(name, type, mtime, size, uid, gid, mode),
        _link(NULL) {
    _parsed = true;
    asprintf(&_link, "%s", link);
  }
  ~Link() {
    free(_link);
  }
  bool        isValid() const { return _type == 'l'; }
  // Data read access
  const char* link()    const { return _link;  }
};

class Socket : public Node {
};

class Stream : public File2 {
  int             _fd;        // file descriptor
  mode_t          _fmode;     // file open mode
  long long       _dsize;     // uncompressed data size, in bytes
  unsigned char*  _fbuffer;   // buffer for file compression during read/write
  bool            _fempty;    // compression buffer not empty
  EVP_MD_CTX*     _ctx;       // openssl resources
  z_stream*       _strm;      // zlib resources
  // Convert MD5 to readable string
  static void md5sum(char* out, const unsigned char* in, int bytes);
public:
  // Max buffer size for read/write
  static const size_t chunk = 409600;
  // Constructor for path in the VFS
  Stream(const char *path, const char* name = "") :
      File2(path, name),
      _fd(-1) {}
  // Open file, for read or write (no append), with or without compression
  int open(
    const char*           req_mode,
    unsigned int          compression = 0);
  // Close file, for read or write (no append), with or without compression
  int close();
  // Read file sets eof (check with eof()) when all data is read and ready
  ssize_t read(
    unsigned char*        buffer,
    size_t                count);
  // Write to file (signal end of file for compression to end properly)
  ssize_t write(
    unsigned char*        buffer,
    size_t                count,
    bool                  eof);
  // Compute file checksum
  int       computeChecksum();
  // Copy file into another
  int       copy(Stream& source);
  // Data access
  long long dsize() const   { return _dsize; };
};

class File {
  string          _path;      // file path
  string          _checksum;  // file checksum
  string          _link;      // what the link points to (if a link, of course)
  char            _type;      // file type ('?' if metadata not available)
  time_t          _mtime;     // time of last modification
  long long       _size;      // on-disk size, in bytes
  uid_t           _uid;       // user ID of owner
  gid_t           _gid;       // group ID of owner
  mode_t          _mode;      // permissions
public:
  // Constructor for existing file (if only one argument, it will be the path)
  File(const string& access_path, const string& path = "");
  // Constructor for given file data
  File(
    const string& path,
    const string& link,
    char          type,
    time_t        mtime,
    long long     size,
    uid_t         uid,
    gid_t         gid,
    mode_t        mode) :
      _path(path),
      _checksum(""),
      _link(link),
      _type(type),
      _mtime(mtime),
      _size(size),
      _uid(uid),
      _gid(gid),
      _mode(mode) {}
  // Constructor for given line
  File(char* line, size_t size);
  // Need '<' to sort list
  bool operator<(const File&) const;
  bool operator!=(const File&) const;
  bool operator==(const File& right) const { return ! (*this != right); }
  bool metadiffer(const File&) const;
  string name() const;
  string path() const { return _path; };
  string link() const { return _link; };
  string checksum() const { return _checksum; }
  string fullPath(int max_size = -1) const;
  char type() const { return _type; }
  time_t mtime() const { return _mtime; };
  long long size() const { return _size; };
  uid_t uid() const { return _uid; };
  gid_t gid() const { return _gid; };
  mode_t mode() const { return _mode; };
  // Line containing all data (argument for debug only)
  string line(bool nodates = false) const;
  void setPath(const string& path) { _path = path; }

  // Read parameters from line
  static int decodeLine(const string& line, list<string>& params);
};

}

#endif
