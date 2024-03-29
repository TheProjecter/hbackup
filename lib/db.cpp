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

/* Compression to use when required: gzip -5 (best speed/ratio) */

/* List file contents:
 *  prefix        (given in the format: 'protocol://host')
 *  path          (metadata)
 *  type          (metadata)
 *  size          (metadata)
 *  modified time (metadata)
 *  owner         (metadata)
 *  group         (metadata)
 *  permissions   (metadata)
 *  link          (readlink)
 *  checksum      (database)
 *  date in       (database)
 *  date out      (database)
 *  mark
 */

#include <iostream>
#include <sstream>
#include <string>
#include <list>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>

using namespace std;

#include "strings.h"
#include "files.h"
#include "dbdata.h"
#include "list.h"
#include "db.h"
#include "hbackup.h"

using namespace hbackup;

struct Database::Private {
  DbList::iterator  entry;
  DbList            active;
  List*             list;
  List*             journal;
};

int Database::organise(const string& path, int number) {
  DIR           *directory;
  struct dirent *dir_entry;
  File          nofiles(path.c_str(), ".nofiles");
  int           failed   = 0;

  /* Already organised? */
  if (nofiles.isValid()) {
    return 0;
  }
  /* Find out how many entries */
  if ((directory = opendir(path.c_str())) == NULL) {
    return 1;
  }
  // Skip . and ..
  number += 2;
  while (((dir_entry = readdir(directory)) != NULL) && (number > 0)) {
    number--;
  }
  /* Decide what to do */
  if (number == 0) {
    rewinddir(directory);
    while ((dir_entry = readdir(directory)) != NULL) {
      /* Ignore . and .. */
      if (! strcmp(dir_entry->d_name, ".")
       || ! strcmp(dir_entry->d_name, "..")) {
        continue;
      }
      Node node(path.c_str(), dir_entry->d_name);
      string source_path = path + "/" + dir_entry->d_name;
      if (node.type() == '?') {
        cerr << "db: organise: cannot get metadata: " << source_path << endl;
        failed = 2;
      } else
      if ((node.type() == 'd')
       // If we crashed, we might have some two-letter dirs already
       && (strlen(node.name()) != 2)
       // If we've reached the point where the dir is ??-?, stop!
       && (node.name()[2] != '-')) {
        // Create two-letter directory
        string dir_path = path + "/" + node.name()[0] + node.name()[1];
        if (Directory("").create(dir_path.c_str())) {
          failed = 2;
        } else {
          // Create destination path
          string dest_path = dir_path + "/" + &node.name()[2];
          // Move directory accross, changing its name
          if (rename(source_path.c_str(), dest_path.c_str())) {
            failed = 1;
          }
        }
      }
    }
    if (! failed) {
      nofiles.create(path.c_str());
    }
  }
  closedir(directory);
  return failed;
}

int Database::write(
    const string&   path,
    char**          dchecksum,
    int             compress) {
  string    temp_path;
  string    dest_path;
  string    checksum;
  int       index = 0;
  int       deleteit = 0;
  int       failed = 0;

  Stream source(path.c_str());
  if (source.open("r")) {
    cerr << strerror(errno) << ": " << path << endl;
    return -1;
  }

  /* Temporary file to write to */
  temp_path = _path + "/filedata";
  Stream temp(temp_path.c_str());
  if (temp.open("w", compress)) {
    cerr << strerror(errno) << ": " << temp_path << endl;
    failed = -1;
  } else

  /* Copy file locally */
  if (temp.copy(source)) {
    cerr << strerror(errno) << ": " << path << endl;
    failed = -1;
  }

  source.close();
  temp.close();

  if (failed) {
    return failed;
  }

  /* Get file final location */
  if (getDir(source.checksum(), dest_path, true) == 2) {
    cerr << "db: write: failed to get dir for: " << source.checksum() << endl;
    failed = -1;
  } else {
    /* Make sure our checksum is unique */
    do {
      string  final_path = dest_path + "-";
      bool    differ = false;

      /* Complete checksum with index */
      stringstream ss;
      string str;
      ss << index;
      ss >> str;
      checksum = string(source.checksum()) + "-" + str;
      final_path += str;
      if (! Directory("").create(final_path.c_str())) {
        /* Directory exists */
        File try_file(final_path.c_str(), "data");
        if (try_file.isValid()) {
          /* A file already exists, let's compare */
          File temp_md(temp_path.c_str());

          differ = (try_file.size() != temp_md.size());
        }
      }
      if (! differ) {
        dest_path = final_path;
        break;
      }
      index++;
    } while (true);

    /* Now move the file in its place */
    if (rename(temp_path.c_str(), (dest_path + "/data").c_str())) {
      cerr << "db: write: failed to move file " << temp_path
        << " to " << dest_path << ": " << strerror(errno);
      failed = -1;
    }
  }

  /* If anything failed, delete temporary file */
  if (failed || deleteit) {
    std::remove(temp_path.c_str());
  }

  // Report checksum
  *dchecksum = NULL;
  if (! failed) {
    asprintf(dchecksum, "%s", checksum.c_str());
  }

  // TODO Need to store compression data

  /* Make sure we won't exceed the file number limit */
  if (! failed) {
    /* dest_path is /path/to/checksum */
    unsigned int pos = dest_path.rfind('/');

    if (pos != string::npos) {
      dest_path.erase(pos);
      /* Now dest_path is /path/to */
      organise(dest_path, 256);
    }
  }

  return failed;
}

int Database::lock() {
  string  lock_path;
  FILE    *file;
  bool    failed = false;

  /* Set the database path that we just locked as default */
  lock_path = _path + "/lock";

  /* Try to open lock file for reading: check who's holding the lock */
  if ((file = fopen(lock_path.c_str(), "r")) != NULL) {
    pid_t pid = 0;

    /* Lock already taken */
    fscanf(file, "%d", &pid);
    fclose(file);
    if (pid != 0) {
      /* Find out whether process is still running, if not, reset lock */
      kill(pid, 0);
      if (errno == ESRCH) {
        cerr << "db: lock: lock reset" << endl;
        std::remove(lock_path.c_str());
      } else {
        cerr << "db: lock: lock taken by process with pid " << pid << endl;
        failed = true;
      }
    } else {
      cerr << "db: lock: lock taken by an unidentified process!" << endl;
      failed = true;
    }
  }

  /* Try to open lock file for writing: lock */
  if (! failed) {
    if ((file = fopen(lock_path.c_str(), "w")) != NULL) {
      /* Lock taken */
      fprintf(file, "%u\n", getpid());
      fclose(file);
    } else {
      /* Lock cannot be taken */
      cerr << "db: lock: cannot take lock" << endl;
      failed = true;
    }
  }
  if (failed) {
    return -1;
  }
  return 0;
}

void Database::unlock() {
  string lock_path = _path + "/lock";
  std::remove(lock_path.c_str());
}

int Database::getDir(
    const string& checksum,
    string&       path,
    bool          create) {
  path = _path + "/data";
  int level = 0;

  // Two cases: either there are files, or a .nofiles file and directories
  do {
    // If we can find a .nofiles file, then go down one more directory
    if (File(path.c_str(), ".nofiles").isValid()) {
      path += "/" + checksum.substr(level, 2);
      level += 2;
      if (create && Directory("").create(path.c_str())) {
        return 1;
      }
    } else {
      break;
    }
  } while (true);
  // Return path
  path += "/" + checksum.substr(level);
  return ! Directory(path.c_str()).isValid();
}

int Database::merge() {
  bool failed = false;

  // Merge with existing list into new one
  List list(_path.c_str(), "list.part");
  if (! list.open("w")) {
    if (list.merge(*_d->list, *_d->journal) && (errno != EBUSY)) {
      cerr << "db: close: merge failed" << endl;
      failed = true;
    }
    list.close();
    if (! failed) {
      if (rename((_path + "/list").c_str(), (_path + "/list~").c_str())
      || rename((_path + "/list.part").c_str(), (_path + "/list").c_str())) {
        cerr << "db: close: cannot rename lists" << endl;
        failed = true;
      }
    }
  } else {
    cerr << strerror(errno) << ": failed to open temporary list" << endl;
    failed = true;
  }
  if (failed) {
    return -1;
  }
  return 0;
}

Database::Database(const string& path) {
  _path          = path;
  _d             = new Private;
}

Database::~Database() {
  delete _d;
}

int Database::open() {
  bool failed = false;

  if (! Directory(_path.c_str()).isValid() && mkdir(_path.c_str(), 0755)) {
    cerr << "db: cannot create base directory" << endl;
    return 2;
  }
  // Try to take lock
  if (lock()) {
    errno = ENOLCK;
    return 2;
  }

  List list(_path.c_str(), "list");

  // Check DB dir
  if (! Directory((_path + "/data").c_str()).isValid()) {
    if (Directory(_path.c_str(), "data").create(_path.c_str())) {
      cerr << "db: cannot create data directory" << endl;
      failed = true;
    } else
    if (list.open("w") || list.close()) {
      cerr << "db: cannot create list file" << endl;
      failed = true;
    } else
    if (verbosity() > 2) {
      cout << " --> Database initialized" << endl;
    }
  } else {
    if (! list.isValid()) {
      Stream backup(_path.c_str(), "list~");

      cerr << "db: list not accessible...";
      if (backup.isValid()) {
        cerr << "using backup" << endl;
        rename((_path + "/list~").c_str(), (_path + "/list").c_str());
      } else {
        cerr << "no backup accessible, aborting.";
        failed = true;
      }
    }
  }

  // Open list
  if (! failed) {
    _d->list = new List(_path.c_str(), "list");
    if (_d->list->open("r")) {
      cerr << "db: open: cannot open list" << endl;
      failed = true;
    }
  }

  // Deal with journal
  if (! failed) {
    _d->journal = new List(_path.c_str(), "journal");

    // Check previous crash
    if (! _d->journal->open("r")) {
      cout << "Previous crash detected, attempting recovery" << endl;
      if (merge()) {
        cerr << "db: open: cannot recover from previous crash" << endl;
        failed = true;
      }
      _d->journal->close();
      _d->list->close();

      if (! failed) {
        rename((_path + "/journal").c_str(), (_path + "/journal~").c_str());
        // Re-open list
        if (_d->list->open("r")) {
          cerr << "db: open: cannot re-open list" << endl;
          failed = true;
        }
      }
    }

    // Create journal
    if (! failed && (_d->journal->open("w"))) {
      cerr << "db: open: cannot open journal" << endl;
      failed = true;
    }
  }

  // Read database active items list
  if (! failed) {
    _d->active.clear();
    if (_d->active.open(_path, "list")) {
      failed = true;
    }
  }
  _d->entry = _d->active.begin();

  if (failed) {
      // Close lists
    _d->list->close();
    _d->journal->close();

    // Delete lists
    delete _d->journal;
    delete _d->list;

    // Unlock DB
    unlock();
    return 2;
  }
  if (verbosity() > 2) {
    cout << " --> Database open (contents: "
      << _d->active.size() << " file";
    if (_d->active.size() != 1) {
      cout << "s";
    }
    cout << ")" << endl;
  }
  return 0;
}

int Database::close() {
  bool failed = false;

  // Close lists
  _d->journal->close();
  _d->list->close();

  // Re-open lists
  if (_d->journal->open("r")) {
    cerr << "db: close: cannot re-open journal" << endl;
    failed = true;
  }
  if (_d->list->open("r")) {
    cerr << "db: close: cannot re-open list" << endl;
    failed = true;
  }

  // Merge journal into list
  if (merge()) {
    failed = true;
  }

  // Close lists
  _d->journal->close();
  _d->list->close();

  // Delete lists
  delete _d->journal;
  delete _d->list;

  // Release lock
  unlock();
  if (failed) {
    return -1;
  } else {
    rename((_path + "/journal").c_str(), (_path + "/journal~").c_str());
  }
  if (verbosity() > 2) {
    cout << " --> Database closed" << endl;
  }
  return 0;
}

void Database::getList(
    const char*  prefix,
    const char*  base_path,
    const char*  rel_path,
    list<Node*>& list) {
  char* full_path = NULL;
  int length = asprintf(&full_path, "%s/%s/%s/", prefix, base_path, rel_path);
  if (rel_path[0] == '\0') {
    full_path[--length] = '\0';
  }

  // Look for beginning
  if ((_d->entry == _d->active.end()) && (_d->entry != _d->active.begin())) {
    _d->entry--;
  }
  // Jump irrelevant last records
  while ((_d->entry != _d->active.begin())
      && (_d->entry->pathCompare(full_path, length) >= 0)) {
    _d->entry--;
  }
  // Jump irrelevant first records
  while ((_d->entry != _d->active.end())
      && (_d->entry->pathCompare(full_path) < 0)) {
    _d->entry++;
  }
  // Copy relevant records
  char* last_dir     = NULL;
  int   last_dir_len = 0;
  while ((_d->entry != _d->active.end())
      && (_d->entry->pathCompare(full_path, length) == 0)) {
    if ((last_dir == NULL) || _d->entry->pathCompare(last_dir, last_dir_len)) {
      Node* node;
      switch (_d->entry->data()->type()) {
        case 'f':
          node = new File(*((File*) _d->entry->data()));
          break;
        case 'l':
          node = new Link(*((Link*) _d->entry->data()));
          break;
        default:
          node = new Node(*_d->entry->data());
      }
      if (node->type() == 'd') {
        free(last_dir);
        last_dir = NULL;
        last_dir_len = asprintf(&last_dir, "%s%s/", full_path, node->name());
      }
      list.push_back(node);
    }
    _d->entry++;
  }
  free(last_dir);
  free(full_path);
}

int Database::read(const string& path, const string& checksum) {
  string  source_path;
  string  temp_path;
  string  temp_checksum;
  int     failed = 0;

  if (getDir(checksum, source_path, false)) {
    cerr << "db: read: failed to get dir for: " << checksum << endl;
    return 2;
  }
  source_path += "/data";

  /* Open temporary file to write to */
  temp_path = path + ".part";

  /* Copy file to temporary name (size not checked: checksum suffices) */
  Stream source(source_path.c_str());
  if (source.open("r")) {
    cerr << "db: read: failed to open source file: " << source_path << endl;
    return 2;
  }
  Stream temp(temp_path.c_str());
  if (temp.open("w")) {
    cerr << "db: read: failed to open dest file: " << temp_path << endl;
    failed = 2;
  } else

  if (temp.copy(source)) {
    cerr << "db: read: failed to copy file: " << source_path << endl;
    failed = 2;
  }

  source.close();
  temp.close();

  if (! failed) {
    /* Verify that checksums match before overwriting final destination */
    if (strncmp(source.checksum(), temp.checksum(), strlen(temp.checksum()))) {
      cerr << "db: read: checksums don't match: " << source_path
        << " " << temp_checksum << endl;
      failed = 2;
    } else

    /* All done */
    if (rename(temp_path.c_str(), path.c_str())) {
      cerr << "db: read: failed to rename file to " << strerror(errno)
        << ": " << path << endl;
      failed = 2;
    }
  }

  if (failed) {
    std::remove(temp_path.c_str());
  }

  return failed;
}

int Database::scan(const String& checksum, bool thorough) {
#warning need to report in journal, somehow...
  int failed = 0;

  if (checksum == "") {
    list<String> sums;
    char*       path   = NULL;
    Node*       node   = NULL;

    // Get list of checksums
    while (_d->list->getEntry(NULL, NULL, &path, &node) > 0) {
      if (node->type() == 'f') {
        File *f = (File*) node;
        if (f->checksum()[0] != '\0') {
          sums.push_back(f->checksum());
        }
      }
      if (terminating()) {
        errno = EINTR;
        return -1;
      }
    }
    sums.sort();
    sums.unique();
    if (verbosity() > 2) {
      cout << " --> Scanning database contents";
      if (thorough) {
        cout << " thoroughly";
      }
      cout << ": " << sums.size() << " file";
      if (sums.size() != 1) {
        cout << "s";
      }
      cout << endl;
    }
    for (list<String>::iterator i = sums.begin(); i != sums.end(); i++) {
      scan(i->c_str(), thorough);
      if (terminating()) {
        errno = EINTR;
        return -1;
      }
    }
  } else {
    string  path;
    bool    filefailed = false;

    if (getDir(checksum.c_str(), path, false)) {
      errno = ENODATA;
      filefailed = true;
      cerr << "db: scan: failed to get directory for checksum "
        << checksum.c_str() << endl;
    } else
    if (! File(path.c_str(), "data").isValid()) {
      errno = ENOENT;
      filefailed = true;
      cerr << "db: scan: file data missing for checksum "
        << checksum.c_str() << endl;
    } else
    if (thorough) {
      string check_path = path + "/data";

      /* Read file to compute checksum, compare with expected */
      Stream s(check_path.c_str());
      if (s.computeChecksum()) {
        errno = ENOENT;
        filefailed = true;
        cerr << "db: scan: file data missing for checksum "
          << checksum.c_str() << endl;
      } else
      if (strncmp(checksum.c_str(), s.checksum(), strlen(s.checksum()))) {
        errno = EILSEQ;
        filefailed = true;
        if (! terminating()) {
          cerr << "db: scan: file data corrupted for checksum "
            << checksum.c_str() << " (found to be " << s.checksum() << ")"
            << endl;
        }
      }

      // Remove corrupted file if any
      if (filefailed) {
        std::remove(check_path.c_str());
      }
    }
    if (filefailed) {
      failed = 1;
    }
  }
  return failed;
}

int Database::add(
    const char* prefix,
    const char* base_path,
    const char* rel_path,
    const char* dir_path,
    const Node* node,
    const char* old_checksum) {
  bool failed = false;

  // Add new record to active list
  if ((node->type() == 'l') && ! node->parsed()) {
    cerr << "Bug in db add: link is not parsed!" << endl;
    return -1;
  }

  // Determine path
  char* full_path = NULL;
  if (rel_path[0] != '\0') {
    asprintf(&full_path, "%s/%s/%s", base_path, rel_path, node->name());
  } else {
    asprintf(&full_path, "%s/%s", base_path, node->name());
  }

  // Create data
  Node* node2;
  switch (node->type()) {
    case 'l':
      node2 = new Link(*(Link*)node);
      break;
    case 'f':
      node2 = new File(*node);
      if ((old_checksum != NULL) && (old_checksum[0] != '\0')) {
        // Use same checksum
        ((File*)node2)->setChecksum(old_checksum);
      } else {
        // Copy data
        char* local_path = NULL;
        char* checksum   = NULL;
        asprintf(&local_path, "%s/%s", dir_path, node->name());
        if (! write(string(local_path), &checksum)) {
          ((File*)node2)->setChecksum(checksum);
          free(checksum);
        } else {
          failed = true;
        }
        free(local_path);
      }
      break;
    default:
      node2 = new Node(*node);
  }

  if (! failed || (old_checksum == NULL)) {
    // Add entry info to journal
    _d->journal->added(prefix, full_path, node2,
      (old_checksum != NULL) && (old_checksum[0] == '\0') ? 0 : -1);
  }

  free(full_path);
  if (failed) {
    return -1;
  }
  return 0;
}

void Database::remove(
    const char* prefix,
    const char* base_path,
    const char* rel_path,
    const Node* node) {
  char* full_path = NULL;
  if (rel_path[0] != '\0') {
    asprintf(&full_path, "%s/%s/%s", base_path, rel_path, node->name());
  } else {
    asprintf(&full_path, "%s/%s", base_path, node->name());
  }

  // Add entry info to journal
  _d->journal->removed(prefix, full_path);

  free(full_path);
}

// For debug only
void* Database::active() {
  return &_d->active;
}
