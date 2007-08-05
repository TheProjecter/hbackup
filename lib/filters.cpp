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

#include <iostream>
#include <stdarg.h>
#include <regex.h>
#include <sys/stat.h>

using namespace std;

#include "files.h"
#include "filters.h"

using namespace hbackup;

bool Condition::match(const File& filedata) const {
  string name;

  /* For name checks, get file name */
  if ((_type == filter_name)
   || (_type == filter_name_start)
   || (_type == filter_name_end)
   || (_type == filter_name_regex)) {
    unsigned int pos = filedata.path().rfind("/");
    if (pos == string::npos) {
      name = filedata.path();
    } else {
      name = filedata.path().substr(pos + 1);
    }
  }

  /* Test condition */
  switch(_type) {
  case filter_type:
    return _file_type == filedata.type();
  case filter_name:
    return name == _string;
  case filter_name_start:
    return name.substr(0, _string.size()) == _string;
  case filter_name_end: {
    signed int diff = name.size() - _string.size();
    if (diff < 0) {
      return false;
    }
    return _string == name.substr(diff); }
  case filter_name_regex: {
    regex_t regex;
    if (! regcomp(&regex, _string.c_str(), REG_EXTENDED)) {
      return ! regexec(&regex, name.c_str(), 0, NULL, 0);
    }
    cerr << "filters: regex: incorrect expression" << endl; }
  case filter_path:
    return filedata.path() == _string;
  case filter_path_start:
    return filedata.path().substr(0, _string.size()) == _string;
  case filter_path_end: {
    signed int diff = filedata.path().size() - _string.size();
    if (diff < 0) {
      return false;
    }
    return _string == filedata.path().substr(diff); }
  case filter_path_regex: {
    regex_t regex;
    if (! regcomp(&regex, _string.c_str(), REG_EXTENDED)) {
      return ! regexec(&regex, filedata.path().c_str(), 0, NULL, 0);
    }
    cerr << "filters: regex: incorrect expression" << endl; }
  case filter_size_above:
    return filedata.size() >= _size;
  case filter_size_below:
    return filedata.size() <= _size;
  default:
    cerr << "filters: match: unknown condition type" << endl;
  }
  return false;
}

void Condition::show() const {
  switch (_type) {
    case filter_name:
    case filter_name_end:
    case filter_name_start:
    case filter_name_regex:
    case filter_path:
    case filter_path_end:
    case filter_path_start:
    case filter_path_regex:
      cout << "--> " << _string << " " << _type << endl;
      break;
    case filter_size_above:
    case filter_size_below:
      cout << "--> " << _size << " " << _type << endl;
      break;
    default:
      cout << "--> unknown condition type " << _type << endl;
  }
}

bool Filters::match(const File& filedata) const {
  /* Read through list of rules */
  for (unsigned int i = 0; i < size(); i++) {
    Filter  rule  = (*this)[i];
    bool    match = true;

    /* Read through list of conditions in rule */
    for (unsigned int j = 0; j < rule.size(); j++) {
      Condition condition = rule[j];

      /* All filters must match for rule to match */
      if (! condition.match(filedata)) {
        match = false;
        break;
      }
    }
    /* If all conditions matched, or the rule is empty, we have a rule match */
    if (match) {
      return true;
    }
  }
  /* No match */
  return false;
}