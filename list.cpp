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
#include <string>

#include "list.h"

list_entry_t *list_entry_new() {
  return new list_entry_t;
}

List::List(list_payload_get_f payload_get) {
  _size = 0;
  _payload_get_f = payload_get;
  _first = _last = _hint = NULL;
}

List::~List() {
  if (_size != 0) {
    list_entry_t *entry = _first;

    while (entry != NULL) {
      list_entry_t *dump = entry;

      entry = entry->next;
      free(dump->payload);
      free(dump);
    }
  }
}

string List::payloadString(const void *payload) const {
  return _payload_get_f(payload);
}

list_entry_t *List::find_hidden(
    const string&       search_string,
    list_payload_get_f  payload_get) const {
  list_entry_t *entry = NULL;

  /* Search the list  (hint is only null if the list is empty) */
  if (_size != 0) {
    /* List is ordered, so search from hint to hopefully save time */
    entry = _hint;
    string payload_string = payload_get(entry->payload);
    /* Which direction to go? */
    if (payload_string < search_string) {
      /* List is ordered and hint is before the searched pattern: forward */
      while (payload_string < search_string) {
        entry = entry->next;
        if (entry == NULL) {
          break;
        }
        payload_string = payload_get(entry->payload);
      }
    } else {
      /* List is ordered and hint is after the search pattern: backward */
      while (payload_string > search_string) {
        entry = entry->previous;
        if (entry == NULL) {
          break;
        }
        payload_string = payload_get(entry->payload);
      }
      /* We must always give the next when the record was not found */
      if (entry == NULL) {
        entry = _first;
      } else if (payload_string != search_string) {
        entry = entry->next;
      }
    }
  }
  return entry;
}

void *list_entry_payload(const list_entry_t *entry) {
  return entry->payload;
}

list_entry_t *List::append(void *payload) {
  list_entry_t *entry = list_entry_new();

  if (entry == NULL) {
    fprintf(stderr, "list: append: failed\n");
    return NULL;
  }
  entry->payload = payload;
  entry->previous = _last;
  entry->next = NULL;
  _last = entry;
  if (entry->previous == NULL) {
    _first = entry;
  } else {
    entry->previous->next = entry;
  }
  _hint = entry;
  _size++;
  return entry;
}

list_entry_t *List::add(void *payload) {
  list_entry_t *entry = list_entry_new();

  if (entry == NULL) {
    fprintf(stderr, "list: add: failed\n");
    return NULL;
  }
  entry->payload = payload;
  if (_payload_get_f == NULL) {
    entry->next = NULL;
  } else {
    entry->next = find_hidden(_payload_get_f(entry->payload), _payload_get_f);
  }
  if (entry->next == NULL) {
    entry->previous = _last;
    _last = entry;
  } else {
    entry->previous = entry->next->previous;
    entry->next->previous = entry;
  }
  if (entry->previous == NULL) {
    _first = entry;
  } else {
    entry->previous->next = entry;
  }
  _hint = entry;
  _size++;
  return entry;
}

void *List::remove(list_entry_t *entry) {
  void *payload = entry->payload;

  /* The hint must not be null if the list is not empty */
  if (entry->previous != NULL) {
    /* our previous' next becomes our next */
    entry->previous->next = entry->next;
    _hint = entry->previous;
  } else {
    /* removing the first record: point first to our next */
    _hint = _first = entry->next;
  }
  if (entry->next == NULL) {
    _last = entry->previous;
  } else {
    entry->next->previous = entry->previous;
  }
  free(entry);
  _size--;
  return payload;
}

int List::size() const {
  return _size;
}

list_entry_t *List::next(const list_entry_t *entry) const {
  if (entry == NULL) {
    return _first;
  } else {
    return entry->next;
  }
}

int List::find(
    const string&       search_string,
    list_payload_get_f  payload_get,
    list_entry_t**      entry_handle) const {
  list_entry_t    *entry;

  /* Impossible to search without interpreting the payload */
  if (payload_get == NULL) {
    /* If no function was given, use the default one, or exit */
    if (_payload_get_f == NULL) {
      fprintf(stderr, "list: find: no way to interpret payload\n");
      return 2;
    }
    payload_get = _payload_get_f;
  }

  entry = find_hidden(search_string, payload_get);
  if (entry_handle != NULL) {
    *entry_handle = entry;
  }
  if (entry == NULL) {
    return 1;
  }
  return payload_get(entry->payload) != search_string;
}

void List::show(list_entry_t *entry, list_payload_get_f payload_get) const {
  static int level = 0;

  if (payload_get == NULL) {

    /* If no function was given, use the default one, or exit */
    if (_payload_get_f == NULL) {
      fprintf(stderr, "list: show: no way to interpret payload\n");
      return;
    }
    payload_get = _payload_get_f;
  }

  if (entry != NULL) {
    if (list_entry_payload(entry) != NULL) {
      for (int i = 0; i < level; i++) {
        printf("-");
      }
      cout << "> " << payload_get(list_entry_payload(entry)) << endl;
    } else {
      fprintf(stderr, "list: show: no payload!\n");
    }
  } else {
    level++;
    while ((entry = next(entry)) != NULL) {
      show(entry, payload_get);
    }
    level--;
  }
}

int List::compare(
    const List              *other,
    List                    *added,
    List                    *missing,
    list_payloads_compare_f compare_f) const {
  /* Comparison result */
  int           differ        = 0;

  /* Point the entries to the start of their respective lists */
  list_entry_t  *entry_this   = next(NULL);
  list_entry_t  *entry_other  = other->next(NULL);

  while ((entry_this != NULL) || (entry_other != NULL)) {
    void  *payload_this;
    void  *payload_other;
    int   result;

    if (entry_this != NULL) {
      payload_this  = list_entry_payload(entry_this);
    } else {
      payload_this  = NULL;
    }
    if (entry_other != NULL) {
      payload_other = list_entry_payload(entry_other);
    } else {
      payload_other = NULL;
    }

    if (payload_this == NULL) {
      result = 1;
    } else if (payload_other == NULL) {
      result = -1;
    } else if (compare_f != NULL) {
      result = compare_f(payload_this, payload_other);
    } else {
      result = payloadString(payload_this).compare(
        other->payloadString(payload_other));
    }
    differ |= (result != 0);
    /* left < right => element is missing from right list */
    if ((result < 0) && (missing != NULL)) {
      /* The contents are NOT copied, so the two lists have elements
       * pointing to the same data! */
      missing->append(payload_this);
    }
    /* left > right => element was added in right list */
    if ((result > 0) && (added != NULL)) {
      /* The contents are NOT copied, so the two lists have elements
       * pointing to the same data! */
      added->append(payload_other);
    }
    if (result >= 0) {
      entry_other = other->next(entry_other);
    }
    if (result <= 0) {
      entry_this = next(entry_this);
    }
  }
  if (differ) {
    return 1;
  }
  return 0;
}

int List::select(
    const string&       search_string,
    list_payload_get_f  payload_get,
    List                *selected_p,
    List                *unselected_p) const {
  list_entry_t  *entry = NULL;

  /* Note: list_find does not garantee to find the first element */

  /* Impossible to search without interpreting the payload */
  if ((payload_get == NULL) && (_payload_get_f == NULL)) {
    fprintf(stderr, "list: select: no way to interpret payload\n");
    return 2;
  }

  /* Search the complete list, but stop when no more match is found */
  while ((entry = next(entry)) != NULL) {
    if (entry->payload != NULL) {
      string payload_string;

      if (payload_get != NULL) {
        payload_string = payload_get(entry->payload);
      } else {
        payload_string = _payload_get_f(entry->payload);
      }
      /* Only compare the portion of string of search_string's length */
      int result = payload_string.compare(0, search_string.size(),
        search_string);
      if (result == 0) {
        /* Portions of strings match */
        if (selected_p != NULL) {
          selected_p->append(entry->payload);
        }
      } else if (unselected_p != NULL) {
        /* Portions of strings do not match */
        unselected_p->append(entry->payload);
      } else if ((result == 1) && (payload_get == NULL)) {
        /* When using the default payload function, time can be saved */
        break;
      }
    } else {
      fprintf(stderr, "list: select: found null payload, ignoring\n");
    }
  }
  return 0;
}

int List::deselect() {
  list_entry_t *entry = NULL;

  while ((entry = next(entry)) != NULL) {
    free(entry);
    _size--;
  }
  return _size;
}