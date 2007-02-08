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

#include <iostream>
#include <vector>
#include <string.h>
#include <errno.h>
#include "metadata.h"
#include "common.h"
#include "params.h"
#include "tools.h"
#include "list.h"
#include "filters.h"
#include "parsers.h"
#include "filelist.h"
#include "cvs_parser.h"
#include "db.h"
#include "clients.h"

using namespace std;

typedef struct {
  char    *path;
  Filter  *ignore_handle;
  List    *parsers_handle;
} path_t;

static char *mounted = NULL;

int Client::unmount_share(const char *mount_point) {
  int failed = 0;

  if (mounted != NULL) {
    char *command = NULL;

    free(mounted);
    mounted = NULL;
    asprintf(&command, "umount %s", mount_point);
    if (system(command)) {
      failed = 1;
    }
    free(command);
  }
  return failed;
}

int Client::mount_share(const char *mount_point, const char *client_path) {
  int  result   = 0;
  char *share   = NULL;
  char *command = NULL;

  errno = 0;
  /* Determine share */
  if (! strcmp(_protocol, "nfs")) {
    /* Mount Network File System share (hostname:share) */
    asprintf(&share, "%s:%s", _hostname, client_path);
  } else
  if (! strcmp(_protocol, "smb")) {
    /* Mount SaMBa share (//hostname/share) */
    asprintf(&share, "//%s/%s", _hostname, client_path);
  }

  /* Check what is mounted */
  if (mounted != NULL) {
    if (strcmp(mounted, share)) {
      /* Different share mounted: unmount */
      unmount_share(mount_point);
    } else {
      /* Same share mounted: nothing to do */
      free(share);
      return 0;
    }
  }

  /* Build mount command */
  if (! strcmp(_protocol, "nfs")) {
    asprintf(&command, "mount -t nfs -o ro,noatime,nolock %s %s", share, mount_point);
  } else
  if (! strcmp(_protocol, "smb")) {
    char  *credentials  = NULL;

    if (_username != NULL) {
      char *password = NULL;

      if (_password != NULL) {
        asprintf(&password, ",password=%s", _password);
      } else {
        asprintf(&password, "%s", "");
      }
      asprintf(&credentials, ",username=%s%s", _username, password);
      free(password);
    } else {
      asprintf(&credentials, "%s", "");
    }
    asprintf(&command, "mount -t smbfs -o ro,noatime,nocase,iocharset=utf8,codepage=858%s %s %s > /dev/null 2>&1",
      credentials, share, mount_point);
    free(credentials);
  }

  /* Issue mount command */
  result = system(command);
  if (result != 0) {
    errno = ETIMEDOUT;
    free(share);
  } else {
    mounted = share;
  }
  free(command);
  return result;
}

static char *setAnything(const char *value) {
  if (strlen(value) != 0) {
    char *newvalue = new char[strlen(value) + 1];
    strcpy(newvalue, value);
    return newvalue;
  } else {
    return NULL;
  }
}

Client::Client(const char *value) {
  _name     = setAnything(value);
  _hostname = NULL;
  _ip_address = NULL;
  _protocol = NULL;
  _username = NULL;
  _password = NULL;
  _listfile = NULL;
  if (verbosity() > 2) {
    cout << " --> Client: " << _name << endl;
  }
}

Client::~Client() {
  delete _name;
  delete _hostname;
  delete _ip_address;
  delete _protocol;
  delete _username;
  delete _password;
  delete _listfile;
}

void Client::setHostname(const char *value) {
  _hostname = setAnything(value);
  strtolower(_hostname);
}

void Client::setIpAddress(const char *value) {
  _ip_address = setAnything(value);
}
void Client::setProtocol(const char *value) {
  _protocol = setAnything(value);
}
void Client::setUsername(const char *value) {
  _username = setAnything(value);
}
void Client::setPassword(const char *value) {
  _password = setAnything(value);
}
void Client::setListfile(const char *value) {
  _listfile = setAnything(value);
}

void Client::show() {
  cout << "-> " << _protocol << "://";
  if (_username != NULL) {
    cout << _username;

    if (_password != NULL) {
      cout << ":" << _password;
    }
    cout << "@";
  }
  cout << _hostname;
  if (_ip_address != NULL) {
    cout << "[" << _ip_address << "]";
  }
  cout << " " << _listfile << endl;
}

static int add_filter(Filter *handle, const char *type, const char *string) {
  const char *filter_type;
  const char *delim    = strchr(type, '/');
  mode_t     file_type = 0;

  /* Check whether file type was specified */
  if (delim != NULL) {
    filter_type = delim + 1;
    if (! strncmp(type, "file", delim - type)) {
      file_type = S_IFREG;
    } else if (! strncmp(type, "dir", delim - type)) {
      file_type = S_IFDIR;
    } else if (! strncmp(type, "char", delim - type)) {
      file_type = S_IFCHR;
    } else if (! strncmp(type, "block", delim - type)) {
      file_type = S_IFBLK;
    } else if (! strncmp(type, "pipe", delim - type)) {
      file_type = S_IFIFO;
    } else if (! strncmp(type, "link", delim - type)) {
      file_type = S_IFLNK;
    } else if (! strncmp(type, "socket", delim - type)) {
      file_type = S_IFSOCK;
    } else {
      return 1;
    }
  } else {
    filter_type = type;
    file_type = S_IFMT;
  }

  /* Add specified filter */
  if (! strcmp(filter_type, "path_end")) {
    handle->push_back(new Rule(new Condition(file_type, filter_path_end, string)));
  } else if (! strcmp(filter_type, "path_start")) {
    handle->push_back(new Rule(new Condition(file_type, filter_path_start, string)));
  } else if (! strcmp(filter_type, "path_regexp")) {
    handle->push_back(new Rule(new Condition(file_type, filter_path_regexp, string)));
  } else if (! strcmp(filter_type, "size_below")) {
    handle->push_back(new Rule(new Condition(0, filter_size_below, strtoul(string, NULL, 10))));
  } else if (! strcmp(filter_type, "size_above")) {
    handle->push_back(new Rule(new Condition(0, filter_size_above, strtoul(string, NULL, 10))));
  } else {
    return 1;
  }
  return 0;
}

static int add_parser(List *handle, const char *type, const char *string) {
  parser_mode_t mode;

  /* Determine mode */
  if (! strcmp(type, "mod")) {
    mode = parser_modified;
  } else if (! strcmp(type, "mod+oth")) {
    mode = parser_modifiedandothers;
  } else if (! strcmp(type, "oth")) {
    mode = parser_others;
  } else {
    /* Default */
    mode = parser_controlled;
  }

  /* Add specified parser */
  if (! strcmp(string, "cvs")) {
    parsers_add(handle, mode, cvs_parser_new());
  } else {
    return 1;
  }
  return 0;
}

static int read_listfile(const char *listfilename, List *backups) {
  FILE     *listfile;
  char     *buffer = NULL;
  size_t   size    = 0;
  int      line    = 0;
  path_t *backup = NULL;
  int      failed  = 0;

  /* Open list file */
  if ((listfile = fopen(listfilename, "r")) == NULL) {
    failed = 2;
  } else {
    if (verbosity() > 1) {
      printf(" -> Reading backup list\n");
    }

    /* Read list file */
    while (getline(&buffer, &size, listfile) >= 0) {
      char keyword[256];
      char type[256];
      char *string = new char[size];
      int params = params_readline(buffer, keyword, type, string);

      line++;
      if (params <= 0) {
        if (params < 0) {
          errno = EUCLEAN;
          fprintf(stderr,
            "clients: backup: syntax error in list file %s, line %u\n",
            listfilename, line);
          failed = 1;
        }
      } else if (! strcmp(keyword, "ignore")) {
        if (add_filter(backup->ignore_handle, type, string)) {
          fprintf(stderr,
            "clients: backup: unsupported filter in list file %s, line %u\n",
            listfilename, line);
        }
      } else if (! strcmp(keyword, "parser")) {
        strtolower(string);
        if (add_parser(backup->parsers_handle, type, string)) {
          fprintf(stderr,
            "clients: backup: unsupported parser in list file %s, line %u\n",
            listfilename, line);
        }
      } else if (! strcmp(keyword, "path")) {
        /* New backup entry */
        backup = new path_t;
        backup->ignore_handle = new Filter;
        parsers_new(&backup->parsers_handle);
        asprintf(&backup->path, "%s", string);
        if (verbosity() > 2) {
          printf(" --> Path: %s\n", string);
        }
        no_trailing_slash(backup->path);
        backups->append(backup);
      } else {
        errno = EUCLEAN;
        fprintf(stderr,
          "clients: backup: syntax error in list file %s, line %u\n",
          listfilename, line);
        failed = 1;
      }
      free(string);
    }
    /* Close list file */
    fclose(listfile);
  }
  free(buffer);
  return failed;
}

/* Return 1 to force mount */
static int get_paths(const char *protocol, const char *backup_path,
    const char *mount_point, char **share, char **path) {
  if (! strcmp(protocol, "file")) {
    asprintf(share, "%s", "");
    asprintf(path, "%s", backup_path);
    errno = 0;
    return 0;
  }

  if (! strcmp(protocol, "nfs")) {
    asprintf(share, "%s", backup_path);
    asprintf(path, "%s", mount_point);
    errno = 0;
    return 1;
  }

  if (! strcmp(protocol, "smb")) {
    asprintf(share, "%c$", backup_path[0]);
    strtolower(*share);
    asprintf(path, "%s/%s", mount_point, &backup_path[3]);
    /* Lower case */
    strtolower(*path);
    pathtolinux(*path);
    errno = 0;
    return 1;
  }

  errno = EPROTONOSUPPORT;
  return 2;
}

int Client::backup(
    const char *mount_point,
    bool configcheck) {
  int   failed = 0;
  List  *backups      = new List();
  int   clientfailed  = 0;
  char  *share = NULL;
  char  *listfilename = NULL;

  switch (get_paths(_protocol, _listfile, mount_point, &share, &listfilename)) {
    case 1:
      mount_share(mount_point, share);
      break;
  }
  free(share);
  if (errno != 0) {
    switch (errno) {
      case EPROTONOSUPPORT:
        fprintf(stderr, "clients: backup: %s protocol not supported\n",
          _protocol);
        return 1;
      case ETIMEDOUT:
        if (verbosity() > 0) {
          printf("Client unreachable '%s' using protocol '%s'\n",
            _hostname, _protocol);
        }
        return 0;
    }
  }

  if (verbosity() > 0) {
    printf("Backup client '%s' using protocol '%s'\n", _name, _protocol);
  }

  if (! read_listfile(listfilename, backups)) {
    /* Backup */
    if (backups->size() == 0) {
      failed = 1;
    } else if (! configcheck) {
      list_entry_t *entry = NULL;

      while (((entry = backups->next(entry)) != NULL) && ! clientfailed) {
        path_t *backup = (path_t *) (list_entry_payload(entry));
        char     *backup_path = NULL;

        /* TODO Stop when terminating (=> backup destructor) */
        if (verbosity() > 0) {
          printf("Backup path '%s'\n", backup->path);
          if (verbosity() > 1) {
            printf(" -> Building list of files\n");
          }
        }

        switch (get_paths(_protocol, backup->path, mount_point, &share, &backup_path)) {
          case 1:
            if (mount_share(mount_point, share)) {
              clientfailed = 1;
            }
            break;
          case 2:
            clientfailed = 1;
        }
        free(share);

        if ( ! clientfailed
          && ! filelist_new(backup_path, backup->ignore_handle,
            backup->parsers_handle)) {
          char *prefix = NULL;

          if (verbosity() > 1) {
            printf(" -> Parsing list of files\n");
          }
          asprintf(&prefix, "%s://%s", _protocol, _hostname);
          strtolower(backup->path);
          pathtolinux(backup->path);
          if (db_parse(prefix, backup->path, backup_path,
              filelist_get(), 100)) {
            failed        = 1;
          }
          free(prefix);
          filelist_free();
        } else {
          // prepare_share sets errno
          if (! terminating()) {
            fprintf(stderr, "clients: backup: list creation failed\n");
          }
          failed        = 1;
          clientfailed  = 1;
        }
        free(backup_path);

        /* Free encapsulated data */
        free(backup->path);
        delete backup->ignore_handle;
        parsers_free(backup->parsers_handle);
      }
    }
  } else {
    // errno set by functions called
    switch (errno) {
      case ENOENT:
        fprintf(stderr, "clients: backup: list file not found %s\n",
          listfilename);
        break;
      case EUCLEAN:
        fprintf(stderr, "clients: backup: list file corrupted %s\n",
          listfilename);
    }
    failed = 1;
  }
  /* Free backups list */
  delete backups;
  free(listfilename);
  unmount_share(mount_point); // does not change errno
  return failed;
}
