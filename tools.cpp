/*
     Copyright (C) 2006  Herve Fache

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

using namespace std;

#include <iostream>
#include <fstream>
#include <string>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <zlib.h>

#include "metadata.h"
#include "common.h"
#include "tools.h"

#define CHUNK 409600

int testdir(const string& path, bool create) {
  DIR  *directory;

  if ((directory = opendir(path.c_str())) == NULL) {
    if (create && mkdir(path.c_str(), 0777)) {
      cerr << "Failed to create directory: " << path << endl;
      return 2;
    }
    return 1;
  }
  closedir(directory);
  return 0;
}

int testfile(const string& path, bool create) {
  fstream file(path.c_str(), fstream::in);

  if (! file.is_open()) {
    if (create) {
      file.open(path.c_str(), fstream::out);
      if (! file.is_open()) {
        cerr << "db: failed to create file: " << path << endl;
        return 2;
      }
      file.close();
    }
    return 1;
  }
  file.close();
  return 0;
}

char type_letter(mode_t mode) {
  if (S_ISREG(mode))  return 'f';
  if (S_ISDIR(mode))  return 'd';
  if (S_ISCHR(mode))  return 'c';
  if (S_ISBLK(mode))  return 'b';
  if (S_ISFIFO(mode)) return 'p';
  if (S_ISLNK(mode))  return 'l';
  if (S_ISSOCK(mode)) return 's';
  return '?';
}

mode_t type_mode(char letter) {
  if (letter == 'f') return S_IFREG;
  if (letter == 'd') return S_IFDIR;
  if (letter == 'c') return S_IFCHR;
  if (letter == 'b') return S_IFBLK;
  if (letter == 'p') return S_IFIFO;
  if (letter == 'l') return S_IFLNK;
  if (letter == 's') return S_IFSOCK;
  return 0;
}

static void md5sum(const char *checksum, int bytes) {
  char *hex            = "0123456789abcdef";
  unsigned char *copy  = new unsigned char[bytes];
  unsigned char *read  = copy;
  unsigned char *write = (unsigned char *) checksum;

  memcpy(copy, checksum, bytes);
  while (bytes != 0) {
    *write++ = hex[*read >> 4];
    *write++ = hex[*read & 0xf];
    read++;
    bytes--;
  }
  *write = '\0';
  delete copy;
}

int getdir(const string& db_path, const string &checksum, string& path) {
  path = db_path + "/data";
  int level = 0;

  /* Two cases: either there are files, or a .nofiles file and directories */
  do {
    /* If we can find a .nofiles file, then go down one more directory */
    string  temp_path = path + "/.nofiles";
    int     status = testfile(temp_path, false);
    if (! status) {
      path += "/" + checksum.substr(level, 2);
      level += 2;
    } else {
      break;
    }
  } while (true);

  /* Return path */
  path += "/" + checksum.substr(level);

  return testdir(path, true);
}

int zcopy(
    const string& source_path,
    const string& dest_path,
    off_t         *size_in,
    off_t         *size_out,
    char          *checksum_in,
    char          *checksum_out,
    int           compress) {
  FILE          *writefile;
  FILE          *readfile;
  unsigned char buffer_in[CHUNK];
  unsigned char buffer_out[CHUNK];
  EVP_MD_CTX    ctx_in;
  EVP_MD_CTX    ctx_out;
  size_t        length;
  z_stream      strm;

  /* Initialise */
  if (size_in != NULL)  *size_in  = 0;
  if (size_out != NULL) *size_out = 0;

  /* Open file to read from */
  if ((readfile = fopen(source_path.c_str(), "r")) == NULL) {
    return 2;
  }

  /* Open file to write to */
  if ((writefile = fopen(dest_path.c_str(), "w")) == NULL) {
    fclose(readfile);
    return 2;
  }

  /* Create zlib resources */
  if (compress != 0) {
    /* Create openssl resources */
    EVP_DigestInit(&ctx_out, EVP_md5());

    strm.zalloc   = Z_NULL;
    strm.zfree    = Z_NULL;
    strm.opaque   = Z_NULL;
    strm.avail_in = 0;
    strm.next_in  = Z_NULL;
    if (compress > 0) {
      /* Compress */
      if (deflateInit2(&strm, compress, Z_DEFLATED, 16 + 15, 9,
          Z_DEFAULT_STRATEGY)) {
        fprintf(stderr, "zcopy: deflate init failed\n");
        compress = 0;
      }
    } else {
      /* De-compress */
      if (inflateInit2(&strm, 32 + 15)) {
        compress = 0;
      }
    }
  }

  /* Create openssl resources */
  EVP_DigestInit(&ctx_in, EVP_md5());

  /* We shall copy, (de-)compress and compute the checksum in one go */
  while (! feof(readfile) && ! terminating()) {
    size_t rlength = fread(buffer_in, 1, CHUNK, readfile);
    size_t wlength;

    /* Size read */
    if (size_in != NULL) *size_in += rlength;

    /* Checksum computation */
    EVP_DigestUpdate(&ctx_in, buffer_in, rlength);

    /* Compression */
    if (compress != 0) {
      strm.avail_in = rlength;
      strm.next_in = buffer_in;

      do {
        strm.avail_out = CHUNK;
        strm.next_out = buffer_out;
        if (compress > 0) {
          deflate(&strm, feof(readfile) ? Z_FINISH : Z_NO_FLUSH);
        } else {
          switch (inflate(&strm, Z_NO_FLUSH)) {
            case Z_NEED_DICT:
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
              fprintf(stderr, "zcopy: inflate failed\n");
              break;
          }
        }
        rlength = CHUNK - strm.avail_out;

        /* Checksum computation */
        EVP_DigestUpdate(&ctx_out, buffer_out, rlength);

        /* Size to write */
        if (size_out != NULL) *size_out += rlength;

        do {
          wlength = fwrite(buffer_out, 1, rlength, writefile);
          rlength -= wlength;
        } while ((rlength != 0) && (wlength != 0));
      } while (strm.avail_out == 0);
    } else {
      do {
        wlength = fwrite(buffer_in, 1, rlength, writefile);
        rlength -= wlength;
      } while ((rlength != 0) && (wlength != 0));
    }
  }
  fclose(readfile);
  fclose(writefile);

  /* Get checksum for input file */
  if (checksum_in != NULL) {
    EVP_DigestFinal(&ctx_in, (unsigned char *) checksum_in, &length);
    md5sum(checksum_in, length);
  }

  /* Destroy zlib resources */
  if (compress != 0) {
    /* Get checksum for output file */
    if (checksum_out != NULL) {
      EVP_DigestFinal(&ctx_out, (unsigned char *) checksum_out, &length);
      md5sum(checksum_out, length);
    }

    if (compress > 0) {
      deflateEnd(&strm);
    } else
    if (compress < 0) {
      inflateEnd(&strm);
    }
  } else {
    /* Might want the original checksum in the output */
    if (checksum_out != NULL) {
      if (checksum_in != NULL) {
        strcpy(checksum_out, checksum_in);
      } else {
        EVP_DigestFinal(&ctx_in, (unsigned char *) checksum_out, &length);
        md5sum(checksum_out, length);
      }
    }
  }
  return 0;
}

int getchecksum(const string& path, const char *checksum) {
  FILE       *readfile;
  EVP_MD_CTX ctx;
  size_t     rlength;

  /* Open file */
  if ((readfile = fopen(path.c_str(), "r")) == NULL) {
    return 2;
  }

  /* Initialize checksum calculation */
  EVP_DigestInit(&ctx, EVP_md5());

  /* Read file, updating checksum */
  while (! feof(readfile) && ! terminating()) {
    char buffer[CHUNK];

    rlength = fread(buffer, 1, CHUNK, readfile);
    EVP_DigestUpdate(&ctx, buffer, rlength);
  }
  fclose(readfile);

  /* Get checksum */
  EVP_DigestFinal(&ctx, (unsigned char *) checksum, &rlength);
  md5sum(checksum, rlength);

  return 0;
}

int params_readline(string line, char *keyword, char *type,
    string *s) {
  unsigned int  pos;
  unsigned int  minpos;
  int           param_count = 0;

  // Reset all
  strcpy(keyword, "");
  strcpy(type, "");
  *s = "";

  // Get rid of comments
  pos = line.find("#");
  if (pos != string::npos) {
    line.erase(pos);
  }

  // Remove leading blanks
  pos = 0;
  while ((pos < line.size()) && ((line[pos] == ' ') || (line[pos] == '\t'))) {
    pos++;
  }
  line.erase(0, pos);

  // Make sure there is something left to deal with
  if (line.size() == 0) {
    return param_count;
  }

  // Remove trailing blanks
  pos = line.size() - 1;
  while ((pos > 0) && ((line[pos] == ' ') || (line[pos] == '\t'))) {
    pos--;
  }
  if (pos < (line.size() - 1)) {
    line.erase(pos + 1);
  }

  // Make sure there is something left to deal with
  if (line.size() == 0) {
    return 0;
  }

  // Find next blank
  minpos = line.find(" ");
  pos = line.find("\t");
  if (pos == string::npos) {
    if (minpos == string::npos) {
      pos = line.size();
    } else {
      pos = minpos;
    }
  } else
  if ((minpos != string::npos) && (pos > minpos)) {
    pos = minpos;
  }

  // Extract keyword
  string arg1 = line.substr(0, pos);
  strcpy(keyword, arg1.c_str());
  param_count++;

  // Remove keyword and all leading blanks
  pos++;
  while ((pos < line.size()) && ((line[pos] == ' ') || (line[pos] == '\t'))) {
    pos++;
  }
  line.erase(0, pos);

  // Make sure there is something left to deal with
  if (line.size() == 0) {
    return param_count;
  }

  // Find last blank or double quote
  if (line[line.size() - 1] == '\"') {
    line.erase(line.size() - 1, 1);
    pos = line.rfind("\"");
    if (pos == string::npos) {
      return -1;
    } else {
      line.erase(pos, 1);
    }
  } else {
    minpos = line.rfind(" ");
    pos = line.rfind("\t");
    if (pos == string::npos) {
      if (minpos == string::npos) {
        pos = 0;
      } else {
        pos = minpos + 1;
      }
    } else {
      if ((minpos != string::npos) && (pos < minpos)) {
        pos = minpos + 1;
      } else {
        pos++;
      }
    }
  }

  // Extract and remove string
  *s = line.substr(pos);

  // Check string
  if ((*s)[0] == '\"') {
    return -1;
  }

  // Remove string
  line.erase(pos);
  param_count++;

  // Remove trailing blanks
  pos = line.size() - 1;
  while ((pos > 0) && ((line[pos] == ' ') || (line[pos] == '\t'))) {
    pos--;
  }
  if (pos < (line.size() - 1)) {
    line.erase(pos + 1);
  }

  // Extract type
  if (line.size() != 0) {
    strcpy(type, line.c_str());
    param_count++;
  }

  return param_count;
}