/*************************************************************************************************
 * The utility API of Tokyo Cabinet
 *                                                      Copyright (C) 2006-2007 Mikio Hirabayashi
 * This file is part of Tokyo Cabinet.
 * Tokyo Cabinet is free software; you can redistribute it and/or modify it under the terms of
 * the GNU Lesser General Public License as published by the Free Software Foundation; either
 * version 2.1 of the License or any later version.  Tokyo Cabinet is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 * You should have received a copy of the GNU Lesser General Public License along with Tokyo
 * Cabinet; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307 USA.
 *************************************************************************************************/


#include "tcutil.h"
#include "myconf.h"


/*************************************************************************************************
 * basic utilities
 *************************************************************************************************/


/* String containing the version information. */
const char *tcversion = _TC_VERSION;


/* Call back function for handling a fatal error. */
void (*tcfatalfunc)(const char *message) = NULL;


/* Allocate a region on memory. */
void *tcmalloc(size_t size){
  assert(size > 0 && size < INT_MAX);
  char *p = malloc(size);
  if(!p) tcmyfatal("out of memory");
  return p;
}


/* Allocate a nullified region on memory. */
void *tccalloc(size_t nmemb, size_t size){
  assert(nmemb > 0 && size < INT_MAX && size > 0 && size < INT_MAX);
  char *p = calloc(nmemb, size);
  if(!p) tcmyfatal("out of memory");
  return p;
}


/* Re-allocate a region on memory. */
void *tcrealloc(void *ptr, size_t size){
  assert(size > 0);
  char *p = realloc(ptr, size);
  if(!p) tcmyfatal("out of memory");
  return p;
}


/* Duplicate a region on memory. */
void *tcmemdup(const void *ptr, size_t size){
  assert(ptr && size >= 0);
  char *p = tcmalloc(size + 1);
  memcpy(p, ptr, size);
  p[size] = '\0';
  return p;
}


/* Duplicate a string on memory. */
char *tcstrdup(const void *str){
  assert(str);
  int size = strlen(str);
  char *p = tcmalloc(size + 1);
  memcpy(p, str, size);
  p[size] = '\0';
  return p;
}


/* Free a region on memory. */
void tcfree(void *ptr){
  free(ptr);
}



/*************************************************************************************************
 * extensible string
 *************************************************************************************************/


#define TC_XSTRUNIT    12                // allocation unit size of an extensible string


/* private function prototypes */
static void tcvxstrprintf(TCXSTR *xstr, const char *format, va_list ap);


/* Create an extensible string object. */
TCXSTR *tcxstrnew(void){
  TCXSTR *xstr = tcmalloc(sizeof(*xstr));
  xstr->ptr = tcmalloc(TC_XSTRUNIT);
  xstr->size = 0;
  xstr->asize = TC_XSTRUNIT;
  xstr->ptr[0] = '\0';
  return xstr;
}


/* Create an extensible string object from a character string. */
TCXSTR *tcxstrnew2(const char *str){
  assert(str);
  TCXSTR *xstr;
  xstr = tcmalloc(sizeof(*xstr));
  int size = strlen(str);
  int asize = tclmax(size + 1, TC_XSTRUNIT);
  xstr->ptr = tcmalloc(asize);
  xstr->size = size;
  xstr->asize = asize;
  memcpy(xstr->ptr, str, size + 1);
  return xstr;
}


/* Create an extensible string object with the initial allocation size. */
TCXSTR *tcxstrnew3(int asiz){
  assert(asiz >= 0);
  asiz = tclmax(asiz, TC_XSTRUNIT);
  TCXSTR *xstr = tcmalloc(sizeof(*xstr));
  xstr->ptr = tcmalloc(asiz);
  xstr->size = 0;
  xstr->asize = asiz;
  xstr->ptr[0] = '\0';
  return xstr;
}


/* Copy an extensible string object. */
TCXSTR *tcxstrdup(const TCXSTR *xstr){
  assert(xstr);
  TCXSTR *nxstr;
  nxstr = tcmalloc(sizeof(*nxstr));
  int asize = tclmax(xstr->size + 1, TC_XSTRUNIT);
  nxstr->ptr = tcmalloc(asize);
  nxstr->size = xstr->size;
  nxstr->asize = asize;
  memcpy(nxstr->ptr, xstr->ptr, xstr->size + 1);
  return nxstr;
}


/* Delete an extensible string object. */
void tcxstrdel(TCXSTR *xstr){
  assert(xstr);
  free(xstr->ptr);
  free(xstr);
}


/* Concatenate a region to the end of an extensible string object. */
void tcxstrcat(TCXSTR *xstr, const void *ptr, int size){
  assert(xstr && ptr && size >= 0);
  int nsize = xstr->size + size + 1;
  if(xstr->asize < nsize){
    while(xstr->asize < nsize){
      xstr->asize *= 2;
      if(xstr->asize < nsize) xstr->asize = nsize;
    }
    xstr->ptr = tcrealloc(xstr->ptr, xstr->asize);
  }
  memcpy(xstr->ptr + xstr->size, ptr, size);
  xstr->size += size;
  xstr->ptr[xstr->size] = '\0';
}


/* Concatenate a character string to the end of an extensible string object. */
void tcxstrcat2(TCXSTR *xstr, const char *str){
  assert(xstr && str);
  int size = strlen(str);
  int nsize = xstr->size + size + 1;
  if(xstr->asize < nsize){
    while(xstr->asize < nsize){
      xstr->asize *= 2;
      if(xstr->asize < nsize) xstr->asize = nsize;
    }
    xstr->ptr = tcrealloc(xstr->ptr, xstr->asize);
  }
  memcpy(xstr->ptr + xstr->size, str, size + 1);
  xstr->size += size;
}


/* Get the pointer of the region of an extensible string object. */
const void *tcxstrptr(const TCXSTR *xstr){
  assert(xstr);
  return xstr->ptr;
}


/* Get the size of the region of an extensible string object. */
int tcxstrsize(const TCXSTR *xstr){
  assert(xstr);
  return xstr->size;
}


/* Clear an extensible string object. */
void tcxstrclear(TCXSTR *xstr){
  assert(xstr);
  xstr->size = 0;
  xstr->ptr[0] = '\0';
}


/* Convert an extensible string object into a usual allocated region. */
void *tcxstrtomalloc(TCXSTR *xstr){
  assert(xstr);
  char *ptr;
  ptr = xstr->ptr;
  free(xstr);
  return ptr;
}


/* Create an extensible string object from an allocated region. */
TCXSTR *tcxstrfrommalloc(void *ptr, int size){
  TCXSTR *xstr = tcmalloc(sizeof(*xstr));
  xstr->ptr = tcrealloc(ptr, size + 1);
  xstr->ptr[size] = '\0';
  xstr->size = size;
  xstr->asize = size;
  return xstr;
}


/* Perform formatted output into an extensible string object. */
void tcxstrprintf(TCXSTR *xstr, const char *format, ...){
  assert(xstr && format);
  va_list ap;
  va_start(ap, format);
  tcvxstrprintf(xstr, format, ap);
  va_end(ap);
}


/* Allocate a formatted string on memory. */
char *tcsprintf(const char *format, ...){
  assert(format);
  TCXSTR *xstr = tcxstrnew();
  va_list ap;
  va_start(ap, format);
  tcvxstrprintf(xstr, format, ap);
  va_end(ap);
  return tcxstrtomalloc(xstr);
}


/* Perform formatted output into an extensible string object. */
static void tcvxstrprintf(TCXSTR *xstr, const char *format, va_list ap){
  assert(xstr && format);
  while(*format != '\0'){
    if(*format == '%'){
      char cbuf[TC_NUMBUFSIZ];
      cbuf[0] = '%';
      int cblen = 1;
      int lnum = 0;
      format++;
      while(strchr("0123456789 .+-hlLz", *format) && *format != '\0' &&
            cblen < TC_NUMBUFSIZ - 1){
        if(*format == 'l' || *format == 'L') lnum++;
        cbuf[cblen++] = *(format++);
      }
      cbuf[cblen++] = *format;
      cbuf[cblen] = '\0';
      int tlen;
      char *tmp, tbuf[TC_NUMBUFSIZ*2];
      switch(*format){
      case 's':
        tmp = va_arg(ap, char *);
        if(!tmp) tmp = "(null)";
        tcxstrcat2(xstr, tmp);
        break;
      case 'd':
        tlen = sprintf(tbuf, cbuf, va_arg(ap, int));
        tcxstrcat(xstr, tbuf, tlen);
        break;
      case 'o': case 'u': case 'x': case 'X': case 'c':
        if(lnum >= 2){
          tlen = sprintf(tbuf, cbuf, va_arg(ap, unsigned long long));
        } else if(lnum >= 1){
          tlen = sprintf(tbuf, cbuf, va_arg(ap, unsigned long));
        } else {
          tlen = sprintf(tbuf, cbuf, va_arg(ap, unsigned int));
        }
        tcxstrcat(xstr, tbuf, tlen);
        break;
      case 'e': case 'E': case 'f': case 'g': case 'G':
        if(lnum >= 1){
          tlen = sprintf(tbuf, cbuf, va_arg(ap, long double));
        } else {
          tlen = sprintf(tbuf, cbuf, va_arg(ap, double));
        }
        tcxstrcat(xstr, tbuf, tlen);
        break;
      case '@':
        tmp = va_arg(ap, char *);
        if(!tmp) tmp = "(null)";
        while(*tmp){
          switch(*tmp){
          case '&': tcxstrcat(xstr, "&amp;", 5); break;
          case '<': tcxstrcat(xstr, "&lt;", 4); break;
          case '>': tcxstrcat(xstr, "&gt;", 4); break;
          case '"': tcxstrcat(xstr, "&quot;", 6); break;
          default:
            if(!((*tmp >= 0 && *tmp <= 0x8) || (*tmp >= 0x0e && *tmp <= 0x1f)))
              tcxstrcat(xstr, tmp, 1);
            break;
          }
          tmp++;
        }
        break;
      case '?':
        tmp = va_arg(ap, char *);
        if(!tmp) tmp = "(null)";
        while(*tmp){
          unsigned char c = *(unsigned char *)tmp;
          if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || (c != '\0' && strchr("_-.", c))){
            tcxstrcat(xstr, tmp, 1);
          } else {
            tlen = sprintf(tbuf, "%%%02X", c);
            tcxstrcat(xstr, tbuf, tlen);
          }
          tmp++;
        }
        break;
      case '%':
        tcxstrcat(xstr, "%", 1);
        break;
      }
    } else {
      tcxstrcat(xstr, format, 1);
    }
    format++;
  }
}



/*************************************************************************************************
 * array list
 *************************************************************************************************/


#define TC_LISTUNIT    64                // allocation unit number of a list handle


/* private function prototypes */
static int tclistelemcmp(const void *a, const void *b);
static int tclistelemcmpci(const void *a, const void *b);


/* Create a list object. */
TCLIST *tclistnew(void){
  TCLIST *list = tcmalloc(sizeof(*list));
  list->anum = TC_LISTUNIT;
  list->array = tcmalloc(sizeof(list->array[0]) * list->anum);
  list->start = 0;
  list->num = 0;
  return list;
}


/* Create a list object. */
TCLIST *tclistnew2(int anum){
  TCLIST *list = tcmalloc(sizeof(*list));
  if(anum < 1) anum = 1;
  list->anum = anum;
  list->array = tcmalloc(sizeof(list->array[0]) * list->anum);
  list->start = 0;
  list->num = 0;
  return list;
}


/* Copy a list object. */
TCLIST *tclistdup(const TCLIST *list){
  assert(list);
  int num = list->num;
  if(num < 1) tclistnew();
  const TCLISTDATUM *array = list->array + list->start;
  TCLIST *nlist = tcmalloc(sizeof(*nlist));
  TCLISTDATUM *narray = tcmalloc(sizeof(list->array[0]) * tclmax(num, 1));
  for(int i = 0; i < num; i++){
    int size = array[i].size;
    narray[i].ptr = tcmalloc(tclmax(size + 1, TC_XSTRUNIT));
    memcpy(narray[i].ptr, array[i].ptr, size + 1);
    narray[i].size = array[i].size;
  }
  nlist->anum = num;
  nlist->array = narray;
  nlist->start = 0;
  nlist->num = num;
  return nlist;
}


/* Delete a list object. */
void tclistdel(TCLIST *list){
  assert(list);
  TCLISTDATUM *array = list->array;
  int end = list->start + list->num;
  for(int i = list->start; i < end; i++){
    free(array[i].ptr);
  }
  free(list->array);
  free(list);
}


/* Get the number of elements of a list object. */
int tclistnum(const TCLIST *list){
  assert(list);
  return list->num;
}


/* Get the pointer to the region of an element of a list object. */
const void *tclistval(const TCLIST *list, int index, int *sp){
  assert(list && index >= 0 && sp);
  if(index >= list->num) return NULL;
  index += list->start;
  *sp = list->array[index].size;
  return list->array[index].ptr;
}


/* Get the string of an element of a list object. */
const char *tclistval2(const TCLIST *list, int index){
  assert(list && index >= 0);
  if(index >= list->num) return NULL;
  index += list->start;
  return list->array[index].ptr;
}


/* Add an element at the end of a list object. */
void tclistpush(TCLIST *list, const void *ptr, int size){
  assert(list && ptr && size >= 0);
  int index = list->start + list->num;
  if(index >= list->anum){
    list->anum += list->num + 1;
    list->array = tcrealloc(list->array, list->anum * sizeof(list->array[0]));
  }
  TCLISTDATUM *array = list->array;
  array[index].ptr = tcmalloc(tclmax(size + 1, TC_XSTRUNIT));
  memcpy(array[index].ptr, ptr, size);
  array[index].ptr[size] = '\0';
  array[index].size = size;
  list->num++;
}


/* Add a string element at the end of a list object. */
void tclistpush2(TCLIST *list, const char *str){
  assert(list && str);
  int index = list->start + list->num;
  if(index >= list->anum){
    list->anum += list->num + 1;
    list->array = tcrealloc(list->array, list->anum * sizeof(list->array[0]));
  }
  int size = strlen(str);
  TCLISTDATUM *array = list->array;
  array[index].ptr = tcmalloc(tclmax(size + 1, TC_XSTRUNIT));
  memcpy(array[index].ptr, str, size + 1);
  array[index].size = size;
  list->num++;
}


/* Add an allocated element at the end of a list object. */
void tclistpushmalloc(TCLIST *list, void *ptr, int size){
  assert(list && ptr && size >= 0);
  int index = list->start + list->num;
  if(index >= list->anum){
    list->anum += list->num + 1;
    list->array = tcrealloc(list->array, list->anum * sizeof(list->array[0]));
  }
  TCLISTDATUM *array = list->array;
  array[index].ptr = tcrealloc(ptr, size + 1);
  array[index].ptr[size] = '\0';
  array[index].size = size;
  list->num++;
}


/* Remove an element of the end of a list object. */
void *tclistpop(TCLIST *list, int *sp){
  assert(list && sp);
  if(list->num < 1) return NULL;
  int index = list->start + list->num - 1;
  list->num--;
  *sp = list->array[index].size;
  return list->array[index].ptr;
}


/* Remove a string element of the end of a list object. */
char *tclistpop2(TCLIST *list){
  assert(list);
  if(list->num < 1) return NULL;
  int index = list->start + list->num - 1;
  list->num--;
  return list->array[index].ptr;
}


/* Add an element at the top of a list object. */
void tclistunshift(TCLIST *list, const void *ptr, int size){
  assert(list && ptr && size >= 0);
  if(list->start < 1){
    if(list->start + list->num >= list->anum){
      list->anum += list->num + 1;
      list->array = tcrealloc(list->array, list->anum * sizeof(list->array[0]));
    }
    list->start = list->anum - list->num;
    memmove(list->array + list->start, list->array, list->num * sizeof(list->array[0]));
  }
  int index = list->start - 1;
  list->array[index].ptr = tcmalloc(tclmax(size + 1, TC_XSTRUNIT));
  memcpy(list->array[index].ptr, ptr, size);
  list->array[index].ptr[size] = '\0';
  list->array[index].size = size;
  list->start--;
  list->num++;
}


/* Add a string element at the top of a list object. */
void tclistunshift2(TCLIST *list, const char *str){
  assert(list && str);
  if(list->start < 1){
    if(list->start + list->num >= list->anum){
      list->anum += list->num + 1;
      list->array = tcrealloc(list->array, list->anum * sizeof(list->array[0]));
    }
    list->start = list->anum - list->num;
    memmove(list->array + list->start, list->array, list->num * sizeof(list->array[0]));
  }
  int index = list->start - 1;
  int size = strlen(str);
  list->array[index].ptr = tcmalloc(tclmax(size + 1, TC_XSTRUNIT));
  memcpy(list->array[index].ptr, str, size + 1);
  list->array[index].size = size;
  list->start--;
  list->num++;
}


/* Remove an element of the top of a list object. */
void *tclistshift(TCLIST *list, int *sp){
  assert(list && sp);
  if(list->num < 1) return NULL;
  int index = list->start;
  list->start++;
  list->num--;
  *sp = list->array[index].size;
  return list->array[index].ptr;
}


/* Remove a string element of the top of a list object. */
char *tclistshift2(TCLIST *list){
  assert(list);
  if(list->num < 1) return NULL;
  int index = list->start;
  list->start++;
  list->num--;
  return list->array[index].ptr;
}


/* Add an element at the specified location of a list object. */
void tclistinsert(TCLIST *list, int index, const void *ptr, int size){
  assert(list && index >= 0 && ptr && size >= 0);
  if(index > list->num) return;
  index += list->start;
  if(list->start + list->num >= list->anum){
    list->anum += list->num + 1;
    list->array = tcrealloc(list->array, list->anum * sizeof(list->array[0]));
  }
  memmove(list->array + index + 1, list->array + index,
          sizeof(list->array[0]) * (list->start + list->num - index));
  list->array[index].ptr = tcmalloc(tclmax(size + 1, TC_XSTRUNIT));
  memcpy(list->array[index].ptr, ptr, size);
  list->array[index].ptr[size] = '\0';
  list->array[index].size = size;
  list->num++;
}


/* Add a string element at the specified location of a list object. */
void tclistinsert2(TCLIST *list, int index, const char *str){
  assert(list && index >= 0 && str);
  if(index > list->num) return;
  index += list->start;
  if(list->start + list->num >= list->anum){
    list->anum += list->num + 1;
    list->array = tcrealloc(list->array, list->anum * sizeof(list->array[0]));
  }
  memmove(list->array + index + 1, list->array + index,
          sizeof(list->array[0]) * (list->start + list->num - index));
  int size = strlen(str);
  list->array[index].ptr = tcmalloc(tclmax(size + 1, TC_XSTRUNIT));
  memcpy(list->array[index].ptr, str, size);
  list->array[index].ptr[size] = '\0';
  list->array[index].size = size;
  list->num++;
}


/* Remove an element at the specified location of a list object. */
void *tclistremove(TCLIST *list, int index, int *sp){
  assert(list && index >= 0 && sp);
  if(index >= list->num) return NULL;
  index += list->start;
  char *ptr = list->array[index].ptr;
  *sp = list->array[index].size;
  list->num--;
  memmove(list->array + index, list->array + index + 1,
          sizeof(list->array[0]) * (list->start + list->num - index));
  return ptr;
}


/* Remove a string element at the specified location of a list object. */
char *tclistremove2(TCLIST *list, int index){
  assert(list && index >= 0);
  if(index >= list->num) return NULL;
  index += list->start;
  char *ptr = list->array[index].ptr;
  list->num--;
  memmove(list->array + index, list->array + index + 1,
          sizeof(list->array[0]) * (list->start + list->num - index));
  return ptr;
}


/* Overwrite an element at the specified location of a list object. */
void tclistover(TCLIST *list, int index, const void *ptr, int size){
  assert(list && index >= 0 && ptr && size >= 0);
  if(index >= list->num) return;
  index += list->start;
  if(size > list->array[index].size)
    list->array[index].ptr = tcrealloc(list->array[index].ptr, size + 1);
  memcpy(list->array[index].ptr, ptr, size);
  list->array[index].size = size;
  list->array[index].ptr[size] = '\0';
}


/* Overwrite a string element at the specified location of a list object. */
void tclistover2(TCLIST *list, int index, const char *str){
  assert(list && index >= 0 && str);
  if(index >= list->num) return;
  index += list->start;
  int size = strlen(str);
  if(size > list->array[index].size)
    list->array[index].ptr = tcrealloc(list->array[index].ptr, size + 1);
  memcpy(list->array[index].ptr, str, size + 1);
  list->array[index].size = size;
}


/* Sort elements of a list object in lexical order. */
void tclistsort(TCLIST *list){
  assert(list);
  qsort(list->array + list->start, list->num, sizeof(list->array[0]), tclistelemcmp);
}


/* Sort elements of a list object in case-insensitive lexical order. */
void tclistsortci(TCLIST *list){
  assert(list);
  qsort(list->array + list->start, list->num, sizeof(list->array[0]), tclistelemcmpci);
}


/* Search a list object for an element using liner search. */
int tclistlsearch(const TCLIST *list, const void *ptr, int size){
  assert(list && ptr && size >= 0);
  int end = list->start + list->num;
  for(int i = list->start; i < end; i++){
    if(list->array[i].size == size && !memcmp(list->array[i].ptr, ptr, size))
      return i - list->start;
  }
  return -1;
}


/* Search a list object for an element using binary search. */
int tclistbsearch(const TCLIST *list, const void *ptr, int size){
  assert(list && ptr && size >= 0);
  TCLISTDATUM key;
  key.ptr = (char *)ptr;
  key.size = size;
  TCLISTDATUM *res = bsearch(&key, list->array + list->start,
                             list->num, sizeof(list->array[0]), tclistelemcmp);
  return res ? res - list->array - list->start : -1;
}


/* Clear a list object. */
void tclistclear(TCLIST *list){
  assert(list);
  TCLISTDATUM *array = list->array;
  int end = list->start + list->num;
  for(int i = list->start; i < end; i++){
    free(array[i].ptr);
  }
  list->start = 0;
  list->num = 0;
}


/* Serialize a list object into a byte array. */
void *tclistdump(const TCLIST *list, int *sp){
  assert(list && sp);
  const TCLISTDATUM *array = list->array;
  int end = list->start + list->num;
  int tsiz = 0;
  for(int i = list->start; i < end; i++){
    tsiz += array[i].size + sizeof(int);
  }
  char *buf = tcmalloc(tsiz + 1);
  char *wp = buf;
  for(int i = list->start; i < end; i++){
    int step;
    TC_SETVNUMBUF(step, wp, array[i].size);
    wp += step;
    memcpy(wp, array[i].ptr, array[i].size);
    wp += array[i].size;
  }
  *sp = wp - buf;
  return buf;
}


/* Create a list object from a serialized byte array. */
TCLIST *tclistload(const void *ptr, int size){
  assert(ptr && size >= 0);
  TCLIST *list = tcmalloc(sizeof(*list));
  int anum = size / sizeof(int) + 1;
  TCLISTDATUM *array = tcmalloc(sizeof(array[0]) * anum);
  int num = 0;
  const char *rp = ptr;
  const char *ep = (char *)ptr + size;
  while(rp < ep){
    int step, vsiz;
    TC_READVNUMBUF(rp, vsiz, step);
    rp += step;
    if(num >= anum){
      anum *= 2;
      array = tcrealloc(array, anum * sizeof(array[0]));
    }
    array[num].ptr = tcmalloc(tclmax(vsiz + 1, TC_XSTRUNIT));
    memcpy(array[num].ptr, rp, vsiz);
    array[num].ptr[vsiz] = '\0';
    array[num].size = vsiz;
    num++;
    rp += vsiz;
  }
  list->anum = anum;
  list->array = array;
  list->start = 0;
  list->num = num;
  return list;
}


/* Compare two list elements in lexical order.
   `a' specifies the pointer to one element.
   `b' specifies the pointer to the other element.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int tclistelemcmp(const void *a, const void *b){
  assert(a && b);
  char *ao = ((TCLISTDATUM *)a)->ptr;
  char *bo = ((TCLISTDATUM *)b)->ptr;
  int size = (((TCLISTDATUM *)a)->size < ((TCLISTDATUM *)b)->size) ?
    ((TCLISTDATUM *)a)->size : ((TCLISTDATUM *)b)->size;
  for(int i = 0; i < size; i++){
    if(ao[i] > bo[i]) return 1;
    if(ao[i] < bo[i]) return -1;
  }
  return ((TCLISTDATUM *)a)->size - ((TCLISTDATUM *)b)->size;
}


/* Compare two list elements in case-insensitive lexical order..
   `a' specifies the pointer to one element.
   `b' specifies the pointer to the other element.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
static int tclistelemcmpci(const void *a, const void *b){
  assert(a && b);
  TCLISTDATUM *ap = (TCLISTDATUM *)a;
  TCLISTDATUM *bp = (TCLISTDATUM *)b;
  char *ao = ap->ptr;
  char *bo = bp->ptr;
  int size = (ap->size < bp->size) ? ap->size : bp->size;
  for(int i = 0; i < size; i++){
    int ac = ao[i];
    bool ab = false;
    if(ac >= 'A' && ac <= 'Z'){
      ac += 'a' - 'A';
      ab = true;
    }
    int bc = bo[i];
    bool bb = false;
    if(bc >= 'A' && bc <= 'Z'){
      bc += 'a' - 'A';
      bb = true;
    }
    if(ac > bc) return 1;
    if(ac < bc) return -1;
    if(!ab && bb) return 1;
    if(ab && !bb) return -1;
  }
  return ap->size - bp->size;
}



/*************************************************************************************************
 * hash map
 *************************************************************************************************/


#define TC_MAPBNUM     4093              // allocation unit number of a list handle
#define TC_MAPCSUNIT   52                // small allocation unit size of map concatenation
#define TC_MAPCBUNIT   252               // big allocation unit size of map concatenation

/* get the first hash value */
#define TC_MAPHASH1(TC_res, TC_kbuf, TC_ksiz) \
  do { \
    const unsigned char *_TC_p = (const unsigned char *)(TC_kbuf); \
    int _TC_ksiz = TC_ksiz; \
    for((TC_res) = 19780211; _TC_ksiz--;){ \
      (TC_res) = (TC_res) * 37 + *(_TC_p)++; \
    } \
  } while(false)

/* get the second hash value */
#define TC_MAPHASH2(TC_res, TC_kbuf, TC_ksiz) \
  do { \
    const unsigned char *_TC_p = (const unsigned char *)(TC_kbuf) + TC_ksiz - 1; \
    int _TC_ksiz = TC_ksiz; \
    for((TC_res) = 0x13579bdf; _TC_ksiz--;){ \
      (TC_res) = (TC_res) * 31 + *(_TC_p)--; \
    } \
  } while(false)

/* get the size of padding bytes for pointer alignment */
#define TC_ALIGNPAD(TC_hsiz) \
  (((TC_hsiz | ~-(int)sizeof(void *)) + 1) - TC_hsiz)

/* compare two keys */
#define TC_KEYCMP(TC_abuf, TC_asiz, TC_bbuf, TC_bsiz) \
  ((TC_asiz > TC_bsiz) ? 1 : (TC_asiz < TC_bsiz) ? -1 : memcmp(TC_abuf, TC_bbuf, TC_asiz))


/* Create a map object. */
TCMAP *tcmapnew(void){
  return tcmapnew2(TC_MAPBNUM);
}


/* Create a map object with specifying the number of the buckets. */
TCMAP *tcmapnew2(int bnum){
  if(bnum < 1) bnum = 1;
  TCMAP *map = tcmalloc(sizeof(*map));
  TCMAPREC **buckets = tcmalloc(sizeof(map->buckets[0]) * bnum);
  for(int i = 0; i < bnum; i++){
    buckets[i] = NULL;
  }
  map->buckets = buckets;
  map->first = NULL;
  map->last = NULL;
  map->cur = NULL;
  map->bnum = bnum;
  map->rnum = 0;
  return map;
}


/* Copy a map object. */
TCMAP *tcmapdup(const TCMAP *map){
  assert(map);
  TCMAP *nmap = tcmapnew2(tclmax(tclmax(map->bnum, map->rnum), TC_MAPBNUM));
  TCMAPREC *cur = map->cur;
  const char *kbuf;
  int ksiz;
  ((TCMAP *)map)->cur = map->first;
  while((kbuf = tcmapiternext((TCMAP *)map, &ksiz)) != NULL){
    int vsiz;
    const char *vbuf = tcmapiterval(kbuf, &vsiz);
    tcmapputkeep(nmap, kbuf, ksiz, vbuf, vsiz);
  }
  ((TCMAP *)map)->cur = cur;
  return nmap;
}


/* Close a map object. */
void tcmapdel(TCMAP *map){
  assert(map);
  TCMAPREC *rec = map->first;
  while(rec){
    TCMAPREC *next = rec->next;
    free(rec);
    rec = next;
  }
  free(map->buckets);
  free(map);
}


/* Store a record into a map object. */
void tcmapput(TCMAP *map, const void *kbuf, int ksiz, const void *vbuf, int vsiz){
  assert(map && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  unsigned int hash;
  TC_MAPHASH1(hash, kbuf, ksiz);
  int bidx = hash % map->bnum;
  TCMAPREC *rec = map->buckets[bidx];
  TCMAPREC **entp = map->buckets + bidx;
  TC_MAPHASH2(hash, kbuf, ksiz);
  while(rec){
    if(hash > rec->hash){
      entp = &(rec->left);
      rec = rec->left;
    } else if(hash < rec->hash){
      entp = &(rec->right);
      rec = rec->right;
    } else {
      char *dbuf = (char *)rec + sizeof(*rec);
      int kcmp = TC_KEYCMP(kbuf, ksiz, dbuf, rec->ksiz);
      if(kcmp < 0){
        entp = &(rec->left);
        rec = rec->left;
      } else if(kcmp > 0){
        entp = &(rec->right);
        rec = rec->right;
      } else {
        int psiz = TC_ALIGNPAD(ksiz);
        if(vsiz > rec->vsiz){
          TCMAPREC *old = rec;
          rec = tcrealloc(rec, sizeof(*rec) + ksiz + psiz + vsiz + 1);
          if(rec != old){
            if(map->first == old) map->first = rec;
            if(map->last == old) map->last = rec;
            if(map->cur == old) map->cur = rec;
            if(*entp == old) *entp = rec;
            if(rec->prev) rec->prev->next = rec;
            if(rec->next) rec->next->prev = rec;
            dbuf = (char *)rec + sizeof(*rec);
          }
        }
        memcpy(dbuf + ksiz + psiz, vbuf, vsiz);
        dbuf[ksiz+psiz+vsiz] = '\0';
        rec->vsiz = vsiz;
        return;
      }
    }
  }
  int psiz = TC_ALIGNPAD(ksiz);
  rec = tcmalloc(sizeof(*rec) + ksiz + psiz + vsiz + 1);
  char *dbuf = (char *)rec + sizeof(*rec);
  memcpy(dbuf, kbuf, ksiz);
  dbuf[ksiz] = '\0';
  rec->ksiz = ksiz;
  memcpy(dbuf + ksiz + psiz, vbuf, vsiz);
  dbuf[ksiz+psiz+vsiz] = '\0';
  rec->vsiz = vsiz;
  rec->hash = hash;
  rec->left = NULL;
  rec->right = NULL;
  rec->prev = map->last;
  rec->next = NULL;
  *entp = rec;
  if(!map->first) map->first = rec;
  if(map->last) map->last->next = rec;
  map->last = rec;
  map->rnum++;
}


/* Store a string record into a map object. */
void tcmapput2(TCMAP *map, const char *kstr, const char *vstr){
  assert(map && kstr && vstr);
  tcmapput(map, kstr, strlen(kstr), vstr, strlen(vstr));
}


/* Store a new record into a map object. */
bool tcmapputkeep(TCMAP *map, const void *kbuf, int ksiz, const void *vbuf, int vsiz){
  assert(map && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  unsigned int hash;
  TC_MAPHASH1(hash, kbuf, ksiz);
  int bidx = hash % map->bnum;
  TCMAPREC *rec = map->buckets[bidx];
  TCMAPREC **entp = map->buckets + bidx;
  TC_MAPHASH2(hash, kbuf, ksiz);
  while(rec){
    if(hash > rec->hash){
      entp = &(rec->left);
      rec = rec->left;
    } else if(hash < rec->hash){
      entp = &(rec->right);
      rec = rec->right;
    } else {
      char *dbuf = (char *)rec + sizeof(*rec);
      int kcmp = TC_KEYCMP(kbuf, ksiz, dbuf, rec->ksiz);
      if(kcmp < 0){
        entp = &(rec->left);
        rec = rec->left;
      } else if(kcmp > 0){
        entp = &(rec->right);
        rec = rec->right;
      } else {
        return false;
      }
    }
  }
  int psiz = TC_ALIGNPAD(ksiz);
  rec = tcmalloc(sizeof(*rec) + ksiz + psiz + vsiz + 1);
  char *dbuf = (char *)rec + sizeof(*rec);
  memcpy(dbuf, kbuf, ksiz);
  dbuf[ksiz] = '\0';
  rec->ksiz = ksiz;
  memcpy(dbuf + ksiz + psiz, vbuf, vsiz);
  dbuf[ksiz+psiz+vsiz] = '\0';
  rec->vsiz = vsiz;
  rec->hash = hash;
  rec->left = NULL;
  rec->right = NULL;
  rec->prev = map->last;
  rec->next = NULL;
  *entp = rec;
  if(!map->first) map->first = rec;
  if(map->last) map->last->next = rec;
  map->last = rec;
  map->rnum++;
  return true;
}


/* Store a new string record into a map object.
   `map' specifies the map object.
   `kstr' specifies the string of the key.
   `vstr' specifies the string of the value.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, this function has no effect. */
bool tcmapputkeep2(TCMAP *map, const char *kstr, const char *vstr){
  assert(map && kstr && vstr);
  return tcmapputkeep(map, kstr, strlen(kstr), vstr, strlen(vstr));
}


/* Concatenate a value at the end of the value of the existing record in a map object. */
void tcmapputcat(TCMAP *map, const void *kbuf, int ksiz, const void *vbuf, int vsiz){
  assert(map && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  unsigned int hash;
  TC_MAPHASH1(hash, kbuf, ksiz);
  int bidx = hash % map->bnum;
  TCMAPREC *rec = map->buckets[bidx];
  TCMAPREC **entp = map->buckets + bidx;
  TC_MAPHASH2(hash, kbuf, ksiz);
  while(rec){
    if(hash > rec->hash){
      entp = &(rec->left);
      rec = rec->left;
    } else if(hash < rec->hash){
      entp = &(rec->right);
      rec = rec->right;
    } else {
      char *dbuf = (char *)rec + sizeof(*rec);
      int kcmp = TC_KEYCMP(kbuf, ksiz, dbuf, rec->ksiz);
      if(kcmp < 0){
        entp = &(rec->left);
        rec = rec->left;
      } else if(kcmp > 0){
        entp = &(rec->right);
        rec = rec->right;
      } else {
        int psiz = TC_ALIGNPAD(ksiz);
        int asiz = sizeof(*rec) + ksiz + psiz + rec->vsiz + vsiz + 1;
        int unit = (asiz <= TC_MAPCSUNIT) ? TC_MAPCSUNIT : TC_MAPCBUNIT;
        asiz = (asiz - 1) + unit - (asiz - 1) % unit;
        TCMAPREC *old = rec;
        rec = tcrealloc(rec, asiz);
        if(rec != old){
          if(map->first == old) map->first = rec;
          if(map->last == old) map->last = rec;
          if(map->cur == old) map->cur = rec;
          if(*entp == old) *entp = rec;
          if(rec->prev) rec->prev->next = rec;
          if(rec->next) rec->next->prev = rec;
          dbuf = (char *)rec + sizeof(*rec);
        }
        memcpy(dbuf + ksiz + psiz + rec->vsiz, vbuf, vsiz);
        dbuf[ksiz+psiz+rec->vsiz+vsiz] = '\0';
        rec->vsiz += vsiz;
        return;
      }
    }
  }
  int psiz = TC_ALIGNPAD(ksiz);
  int asiz = sizeof(*rec) + ksiz + psiz + vsiz + 1;
  int unit = (asiz <= TC_MAPCSUNIT) ? TC_MAPCSUNIT : TC_MAPCBUNIT;
  asiz = (asiz - 1) + unit - (asiz - 1) % unit;
  rec = tcmalloc(asiz);
  char *dbuf = (char *)rec + sizeof(*rec);
  memcpy(dbuf, kbuf, ksiz);
  dbuf[ksiz] = '\0';
  rec->ksiz = ksiz;
  memcpy(dbuf + ksiz + psiz, vbuf, vsiz);
  dbuf[ksiz+psiz+vsiz] = '\0';
  rec->vsiz = vsiz;
  rec->hash = hash;
  rec->left = NULL;
  rec->right = NULL;
  rec->prev = map->last;
  rec->next = NULL;
  *entp = rec;
  if(!map->first) map->first = rec;
  if(map->last) map->last->next = rec;
  map->last = rec;
  map->rnum++;
}


/* Concatenate a string value at the end of the value of the existing record in a map object. */
void tcmapputcat2(TCMAP *map, const char *kstr, const char *vstr){
  assert(map && kstr && vstr);
  tcmapputcat(map, kstr, strlen(kstr), vstr, strlen(vstr));
}


/* Remove a record of a map object. */
bool tcmapout(TCMAP *map, const void *kbuf, int ksiz){
  assert(map && kbuf && ksiz >= 0);
  unsigned int hash;
  TC_MAPHASH1(hash, kbuf, ksiz);
  int bidx = hash % map->bnum;
  TCMAPREC *rec = map->buckets[bidx];
  TCMAPREC **entp = map->buckets + bidx;
  TC_MAPHASH2(hash, kbuf, ksiz);
  while(rec){
    if(hash > rec->hash){
      entp = &(rec->left);
      rec = rec->left;
    } else if(hash < rec->hash){
      entp = &(rec->right);
      rec = rec->right;
    } else {
      char *dbuf = (char *)rec + sizeof(*rec);
      int kcmp = TC_KEYCMP(kbuf, ksiz, dbuf, rec->ksiz);
      if(kcmp < 0){
        entp = &(rec->left);
        rec = rec->left;
      } else if(kcmp > 0){
        entp = &(rec->right);
        rec = rec->right;
      } else {
        if(rec->prev) rec->prev->next = rec->next;
        if(rec->next) rec->next->prev = rec->prev;
        if(rec == map->first) map->first = rec->next;
        if(rec == map->last) map->last = rec->prev;
        if(rec == map->cur) map->cur = rec->next;
        if(rec->left && !rec->right){
          *entp = rec->left;
        } else if(!rec->left && rec->right){
          *entp = rec->right;
        } else if(!rec->left && !rec->left){
          *entp = NULL;
        } else {
          *entp = rec->left;
          TCMAPREC *tmp = *entp;
          while(tmp->right){
            tmp = tmp->right;
          }
          tmp->right = rec->right;
        }
        free(rec);
        map->rnum--;
        return true;
      }
    }
  }
  return false;
}


/* Remove a string record of a map object. */
bool tcmapout2(TCMAP *map, const void *kstr){
  assert(map && kstr);
  return tcmapout(map, kstr, strlen(kstr));
}


/* Retrieve a record in a map object. */
const void *tcmapget(const TCMAP *map, const void *kbuf, int ksiz, int *sp){
  assert(map && kbuf && ksiz >= 0 && sp);
  unsigned int hash;
  TC_MAPHASH1(hash, kbuf, ksiz);
  TCMAPREC *rec = map->buckets[hash%map->bnum];
  TC_MAPHASH2(hash, kbuf, ksiz);
  while(rec){
    if(hash > rec->hash){
      rec = rec->left;
    } else if(hash < rec->hash){
      rec = rec->right;
    } else {
      char *dbuf = (char *)rec + sizeof(*rec);
      int kcmp = TC_KEYCMP(kbuf, ksiz, dbuf, rec->ksiz);
      if(kcmp < 0){
        rec = rec->left;
      } else if(kcmp > 0){
        rec = rec->right;
      } else {
        *sp = rec->vsiz;
        return dbuf + rec->ksiz + TC_ALIGNPAD(rec->ksiz);
      }
    }
  }
  return NULL;
}


/* Retrieve a string record in a map object. */
const char *tcmapget2(const TCMAP *map, const char *kstr){
  assert(map && kstr);
  int ksiz = strlen(kstr);
  unsigned int hash;
  TC_MAPHASH1(hash, kstr, ksiz);
  TCMAPREC *rec = map->buckets[hash%map->bnum];
  TC_MAPHASH2(hash, kstr, ksiz);
  while(rec){
    if(hash > rec->hash){
      rec = rec->left;
    } else if(hash < rec->hash){
      rec = rec->right;
    } else {
      char *dbuf = (char *)rec + sizeof(*rec);
      int kcmp = TC_KEYCMP(kstr, ksiz, dbuf, rec->ksiz);
      if(kcmp < 0){
        rec = rec->left;
      } else if(kcmp > 0){
        rec = rec->right;
      } else {
        return dbuf + rec->ksiz + TC_ALIGNPAD(rec->ksiz);
      }
    }
  }
  return NULL;
}


/* Move a record to the edge of a map object. */
bool tcmapmove(TCMAP *map, const void *kbuf, int ksiz, bool head){
  assert(map && kbuf && ksiz >= 0);
  unsigned int hash;
  TC_MAPHASH1(hash, kbuf, ksiz);
  TCMAPREC *rec = map->buckets[hash%map->bnum];
  TC_MAPHASH2(hash, kbuf, ksiz);
  while(rec){
    if(hash > rec->hash){
      rec = rec->left;
    } else if(hash < rec->hash){
      rec = rec->right;
    } else {
      char *dbuf = (char *)rec + sizeof(*rec);
      int kcmp = TC_KEYCMP(kbuf, ksiz, dbuf, rec->ksiz);
      if(kcmp < 0){
        rec = rec->left;
      } else if(kcmp > 0){
        rec = rec->right;
      } else {
        if(head){
          if(map->first == rec) return true;
          if(map->last == rec) map->last = rec->prev;
          if(rec->prev) rec->prev->next = rec->next;
          if(rec->next) rec->next->prev = rec->prev;
          rec->prev = NULL;
          rec->next = map->first;
          map->first->prev = rec;
          map->first = rec;
        } else {
          if(map->last == rec) return true;
          if(map->first == rec) map->first = rec->next;
          if(rec->prev) rec->prev->next = rec->next;
          if(rec->next) rec->next->prev = rec->prev;
          rec->prev = map->last;
          rec->next = NULL;
          map->last->next = rec;
          map->last = rec;
        }
        return true;
      }
    }
  }
  return false;
}


/* Move a string record to the edge of a map object. */
bool tcmapmove2(TCMAP *map, const char *kstr, bool head){
  assert(map && kstr);
  return tcmapmove(map, kstr, strlen(kstr), head);
}


/* Initialize the iterator of a map object. */
void tcmapiterinit(TCMAP *map){
  assert(map);
  map->cur = map->first;
}


/* Get the next key of the iterator of a map object. */
const void *tcmapiternext(TCMAP *map, int *sp){
  assert(map && sp);
  TCMAPREC *rec;
  if(!map->cur) return NULL;
  rec = map->cur;
  map->cur = rec->next;
  *sp = rec->ksiz;
  return (char *)rec + sizeof(*rec);
}


/* Get the next key string of the iterator of a map object. */
const char *tcmapiternext2(TCMAP *map){
  assert(map);
  TCMAPREC *rec;
  if(!map->cur) return NULL;
  rec = map->cur;
  map->cur = rec->next;
  return (char *)rec + sizeof(*rec);
}


/* Get the value bound to the key fetched from the iterator of a map object. */
const void *tcmapiterval(const void *kbuf, int *sp){
  assert(kbuf && sp);
  TCMAPREC *rec = (TCMAPREC *)((char *)kbuf - sizeof(*rec));
  *sp = rec->vsiz;
  return (char *)kbuf + rec->ksiz + TC_ALIGNPAD(rec->ksiz);
}


/* Get the value string bound to the key fetched from the iterator of a map object. */
const char *tcmapiterval2(const char *kstr){
  assert(kstr);
  TCMAPREC *rec = (TCMAPREC *)(kstr - sizeof(*rec));
  return kstr + rec->ksiz + TC_ALIGNPAD(rec->ksiz);
}


/* Get the number of records stored in a map object. */
int tcmaprnum(const TCMAP *map){
  assert(map);
  return map->rnum;
}


/* Create a list object containing all keys in a map object. */
TCLIST *tcmapkeys(const TCMAP *map){
  assert(map);
  TCLIST *list = tclistnew2(map->rnum);
  TCMAPREC *cur = map->cur;
  const char *kbuf;
  int ksiz;
  ((TCMAP *)map)->cur = map->first;
  while((kbuf = tcmapiternext((TCMAP *)map, &ksiz)) != NULL){
    tclistpush(list, kbuf, ksiz);
  }
  ((TCMAP *)map)->cur = cur;
  return list;
}


/* Create a list object containing all values in a map object. */
TCLIST *tcmapvals(const TCMAP *map){
  assert(map);
  TCLIST *list = tclistnew2(map->rnum);
  TCMAPREC *cur = map->cur;
  const char *kbuf;
  int ksiz;
  ((TCMAP *)map)->cur = map->first;
  while((kbuf = tcmapiternext((TCMAP *)map, &ksiz)) != NULL){
    int vsiz;
    const char *vbuf = tcmapiterval(kbuf, &vsiz);
    tclistpush(list, vbuf, vsiz);
  }
  ((TCMAP *)map)->cur = cur;
  return list;
}


/* Clear a list object. */
void tcmapclear(TCMAP *map){
  assert(map);
  TCMAPREC *rec = map->first;
  while(rec){
    TCMAPREC *next = rec->next;
    free(rec);
    rec = next;
  }
  TCMAPREC **buckets = map->buckets;
  int bnum = map->bnum;
  for(int i = 0; i < bnum; i++){
    buckets[i] = NULL;
  }
  map->first = NULL;
  map->last = NULL;
  map->cur = NULL;
  map->rnum = 0;
}


/* Serialize map list object into a byte array. */
void *tcmapdump(const TCMAP *map, int *sp){
  assert(map && sp);
  TCMAPREC *cur = map->cur;
  int tsiz = 0;
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  ((TCMAP *)map)->cur = map->first;
  while((kbuf = tcmapiternext((TCMAP *)map, &ksiz)) != NULL){
    vbuf = tcmapiterval(kbuf, &vsiz);
    tsiz += ksiz + vsiz + sizeof(int) * 2;
  }
  char *buf = tcmalloc(tsiz + 1);
  char *wp = buf;
  ((TCMAP *)map)->cur = map->first;
  while((kbuf = tcmapiternext((TCMAP *)map, &ksiz)) != NULL){
    vbuf = tcmapiterval(kbuf, &vsiz);
    int step;
    TC_SETVNUMBUF(step, wp, ksiz);
    wp += step;
    memcpy(wp, kbuf, ksiz);
    wp += ksiz;
    TC_SETVNUMBUF(step, wp, vsiz);
    wp += step;
    memcpy(wp, vbuf, vsiz);
    wp += vsiz;
  }
  ((TCMAP *)map)->cur = cur;
  *sp = wp - buf;
  return buf;
}


/* Create a map object from a serialized byte array. */
TCMAP *tcmapload(const void *ptr, int size){
  assert(ptr && size >= 0);
  TCMAP *map = tcmapnew();
  const char *rp = ptr;
  const char *ep = (char *)ptr + size;
  while(rp < ep){
    int step, ksiz, vsiz;
    TC_READVNUMBUF(rp, ksiz, step);
    rp += step;
    const char *kbuf = rp;
    rp += ksiz;
    TC_READVNUMBUF(rp, vsiz, step);
    rp += step;
    tcmapputkeep(map, kbuf, ksiz, rp, vsiz);
    rp += vsiz;
  }
  return map;
}


/* Extract a map record from a serialized byte array. */
void *tcmaploadone(const void *ptr, int size, const void *kbuf, int ksiz, int *sp){
  assert(ptr && size >= 0 && kbuf && ksiz >= 0 && sp);
  const char *rp = ptr;
  const char *ep = (char *)ptr + size;
  while(rp < ep){
    int step, rsiz;
    TC_READVNUMBUF(rp, rsiz, step);
    rp += step;
    if(rsiz == ksiz && !memcmp(kbuf, rp, rsiz)){
      rp += rsiz;
      TC_READVNUMBUF(rp, rsiz, step);
      rp += step;
      *sp = rsiz;
      return tcmemdup(rp, rsiz);
    }
    rp += rsiz;
    TC_READVNUMBUF(rp, rsiz, step);
    rp += step;
    rp += rsiz;
  }
  return NULL;
}



/*************************************************************************************************
 * memory pool
 *************************************************************************************************/


#define TC_MPOOLUNIT   128               // allocation unit size of memory pool elements


/* Global memory pool object */
TCMPOOL *tcglobalmemorypool = NULL;


/* private function prototypes */
static void tcmpooldelglobal(void);


/* Create a memory pool object. */
TCMPOOL *tcmpoolnew(void){
  TCMPOOL *mpool = tcmalloc(sizeof(*mpool));
  mpool->anum = TC_MPOOLUNIT;
  mpool->elems = tcmalloc(sizeof(mpool->elems[0]) * mpool->anum);
  mpool->num = 0;
  return mpool;
}


/* Delete a memory pool object. */
void tcmpooldel(TCMPOOL *mpool){
  assert(mpool);
  TCMPELEM *elems = mpool->elems;
  for(int i = mpool->num - 1; i >= 0; i--){
    elems[i].del(elems[i].ptr);
  }
  free(elems);
  free(mpool);
}


/* Relegate an arbitrary object to a memory pool object. */
void tcmpoolput(TCMPOOL *mpool, void *ptr, void (*del)(void *)){
  assert(mpool && ptr && del);
  int num = mpool->num;
  if(num >= mpool->anum){
    mpool->anum *= 2;
    mpool->elems = tcrealloc(mpool->elems, mpool->anum * sizeof(mpool->elems[0]));
  }
  mpool->elems[num].ptr = ptr;
  mpool->elems[num].del = del;
  mpool->num++;
}


/* Relegate an allocated region to a memory pool object. */
void tcmpoolputptr(TCMPOOL *mpool, void *ptr){
  assert(mpool && ptr);
  tcmpoolput(mpool, ptr, (void (*)(void *))free);
}


/* Relegate an extensible string object to a memory pool object. */
void tcmpoolputxstr(TCMPOOL *mpool, TCXSTR *xstr){
  assert(mpool && xstr);
  tcmpoolput(mpool, xstr, (void (*)(void *))tcxstrdel);
}


/* Relegate a list object to a memory pool object. */
void tcmpoolputlist(TCMPOOL *mpool, TCLIST *list){
  assert(mpool && list);
  tcmpoolput(mpool, list, (void (*)(void *))tclistdel);
}


/* Relegate a map object to a memory pool object. */
void tcmpoolputmap(TCMPOOL *mpool, TCMAP *map){
  assert(mpool && map);
  tcmpoolput(mpool, map, (void (*)(void *))tcmapdel);
}


/* Allocate a region relegated to a memory pool object. */
void *tcmpoolmalloc(TCMPOOL *mpool, size_t size){
  assert(mpool && size > 0);
  void *ptr = tcmalloc(size);
  tcmpoolput(mpool, ptr, (void (*)(void *))free);
  return ptr;
}


/* Create an extensible string object relegated to a memory pool object. */
TCXSTR *tcmpoolxstrnew(TCMPOOL *mpool){
  assert(mpool);
  TCXSTR *xstr = tcxstrnew();
  tcmpoolput(mpool, xstr, (void (*)(void *))tcxstrdel);
  return xstr;
}


/* Create a list object relegated to a memory pool object. */
TCLIST *tcmpoollistnew(TCMPOOL *mpool){
  assert(mpool);
  TCLIST *list = tclistnew();
  tcmpoolput(mpool, list, (void (*)(void *))tclistdel);
  return list;
}


/* Create a map object relegated to a memory pool object. */
TCMAP *tcmpoolmapnew(TCMPOOL *mpool){
  assert(mpool);
  TCMAP *map = tcmapnew();
  tcmpoolput(mpool, map, (void (*)(void *))tcmapdel);
  return map;
}


/* Get the global memory pool object. */
TCMPOOL *tcmpoolglobal(void){
  if(tcglobalmemorypool) return tcglobalmemorypool;
  tcglobalmemorypool = tcmpoolnew();
  atexit(tcmpooldelglobal);
  return tcglobalmemorypool;
}


/* Detete global memory pool object. */
static void tcmpooldelglobal(void){
  if(tcglobalmemorypool) tcmpooldel(tcglobalmemorypool);
}



/*************************************************************************************************
 * miscellaneous utilities
 *************************************************************************************************/


/* Get the larger value of two integers. */
long tclmax(long a, long b){
  return (a > b) ? a : b;
}


/* Get the lesser value of two integers. */
long tclmin(long a, long b){
  return (a < b) ? a : b;
}


/* Compare two strings with case insensitive evaluation. */
int tcstricmp(const char *astr, const char *bstr){
  assert(astr && bstr);
  while(*astr != '\0'){
    if(*bstr == '\0') return 1;
    int ac = (*astr >= 'A' && *astr <= 'Z') ? *astr + ('a' - 'A') : *(unsigned char *)astr;
    int bc = (*bstr >= 'A' && *bstr <= 'Z') ? *bstr + ('a' - 'A') : *(unsigned char *)bstr;
    if(ac != bc) return ac - bc;
    astr++;
    bstr++;
  }
  return (*bstr == '\0') ? 0 : -1;
}


/* Check whether a string begins with a key. */
bool tcstrfwm(const char *str, const char *key){
  assert(str && key);
  while(*key != '\0'){
    if(*str != *key || *str == '\0') return false;
    key++;
    str++;
  }
  return true;
}


/* Check whether a string begins with a key with case insensitive evaluation. */
bool tcstrifwm(const char *str, const char *key){
  assert(str && key);
  while(*key != '\0'){
    if(*str == '\0') return false;
    int sc = *str;
    if(sc >= 'A' && sc <= 'Z') sc += 'a' - 'A';
    int kc = *key;
    if(kc >= 'A' && kc <= 'Z') kc += 'a' - 'A';
    if(sc != kc) return false;
    key++;
    str++;
  }
  return true;
}


/* Check whether a string ends with a key. */
bool tcstrbwm(const char *str, const char *key){
  assert(str && key);
  int slen = strlen(str);
  int klen = strlen(key);
  for(int i = 1; i <= klen; i++){
    if(i > slen || str[slen-i] != key[klen-i]) return false;
  }
  return true;
}


/* Check whether a string ends with a key with case insensitive evaluation. */
bool tcstribwm(const char *str, const char *key){
  assert(str && key);
  int slen = strlen(str);
  int klen = strlen(key);
  for(int i = 1; i <= klen; i++){
    if(i > slen) return false;
    int sc = str[slen-i];
    if(sc >= 'A' && sc <= 'Z') sc += 'a' - 'A';
    int kc = key[klen-i];
    if(kc >= 'A' && kc <= 'Z') kc += 'a' - 'A';
    if(sc != kc) return false;
  }
  return true;
}


/* Convert the letters of a string into upper case. */
char *tcstrtoupper(char *str){
  assert(str);
  char *wp = str;
  while(*wp != '\0'){
    if(*wp >= 'a' && *wp <= 'z') *wp -= 'a' - 'A';
    wp++;
  }
  return str;
}


/* Convert the letters of a string into lower case. */
char *tcstrtolower(char *str){
  assert(str);
  char *wp = str;
  while(*wp != '\0'){
    if(*wp >= 'A' && *wp <= 'Z') *wp += 'a' - 'A';
    wp++;
  }
  return str;
}


/* Cut space characters at head or tail of a string. */
char *tcstrtrim(char *str){
  assert(str);
  const char *rp = str;
  char *wp = str;
  bool head = true;
  while(*rp != '\0'){
    if(*rp > '\0' && *rp <= ' '){
      if(!head) *(wp++) = *rp;
    } else {
      *(wp++) = *rp;
      head = false;
    }
    rp++;
  }
  *wp = '\0';
  while(wp > str && wp[-1] > '\0' && wp[-1] <= ' '){
    *(--wp) = '\0';
  }
  return str;
}


/* Squeeze space characters in a string and trim it. */
char *tcstrsqzspc(char *str){
  assert(str);
  char *rp = str;
  char *wp = str;
  bool spc = true;
  while(*rp != '\0'){
    if(*rp > 0 && *rp <= ' '){
      if(!spc) *(wp++) = *rp;
      spc = true;
    } else {
      *(wp++) = *rp;
      spc = false;
    }
    rp++;
  }
  *wp = '\0';
  for(wp--; wp >= str; wp--){
    if(*wp > 0 && *wp <= ' '){
      *wp = '\0';
    } else {
      break;
    }
  }
  return str;
}


/* Substitute characters in a string. */
char *tcstrsubchr(char *str, const char *rstr, const char *sstr){
  assert(str && rstr && sstr);
  int slen = strlen(sstr);
  char *wp = str;
  for(int i = 0; str[i] != '\0'; i++){
    const char *p = strchr(rstr, str[i]);
    if(p){
      int idx = p - rstr;
      if(idx < slen) *(wp++) = sstr[idx];
    } else {
      *(wp++) = str[i];
    }
  }
  *wp = '\0';
  return str;
}


/* Count the number of characters in a string of UTF-8. */
int tcstrcntutf(const char *str){
  assert(str);
  const unsigned char *rp = (unsigned char *)str;
  int cnt = 0;
  while(*rp != '\0'){
    if((*rp & 0x80) == 0x00 || (*rp & 0xe0) == 0xc0 ||
       (*rp & 0xf0) == 0xe0 || (*rp & 0xf8) == 0xf0) cnt++;
    rp++;
  }
  return cnt;
}


/* Cut a string of UTF-8 at the specified number of characters. */
char *tcstrcututf(char *str, int num){
  assert(str && num >= 0);
  unsigned char *wp = (unsigned char *)str;
  int cnt = 0;
  while(*wp != '\0'){
    if((*wp & 0x80) == 0x00 || (*wp & 0xe0) == 0xc0 ||
       (*wp & 0xf0) == 0xe0 || (*wp & 0xf8) == 0xf0){
      cnt++;
      if(cnt > num){
        *wp = '\0';
        break;
      }
    }
    wp++;
  }
  return str;
}


/* Create a list object by splitting a string. */
TCLIST *tcstrsplit(const char *str, const char *delim){
  assert(str && delim);
  TCLIST *list = tclistnew();
  while(true){
    const char *sp = str;
    while(*str != '\0' && !strchr(delim, *str)){
      str++;
    }
    tclistpush(list, sp, str - sp);
    if(*str == '\0') break;
    str++;
  }
  return list;
}


/* Get the time of day in milliseconds. */
double tctime(void){
  struct timeval tv;
  struct timezone tz;
  if(gettimeofday(&tv, &tz) == -1) return 0.0;
  return (double)tv.tv_sec * 1000 + (double)tv.tv_usec / 1000;
}



/*************************************************************************************************
 * filesystem utilities
 *************************************************************************************************/


#define TC_FILEMODE    00644             // permission of a creating file
#define TC_IOBUFSIZ    16384             // size of an I/O buffer


/* Read whole data of a file. */
void *tcreadfile(const char *path, int limit, int *sp){
  int fd = path ? open(path, O_RDONLY, TC_FILEMODE) : 0;
  if(fd == -1) return NULL;
  if(fd == 0){
    TCXSTR *xstr = tcxstrnew();
    char buf[TC_IOBUFSIZ];
    limit = limit > 0 ? limit : INT_MAX;
    int rsiz;
    while((rsiz = read(fd, buf, tclmin(TC_IOBUFSIZ, limit))) > 0){
      tcxstrcat(xstr, buf, rsiz);
      limit -= rsiz;
    }
    *sp = tcxstrsize(xstr);
    return tcxstrtomalloc(xstr);
  }
  struct stat sbuf;
  if(fstat(fd, &sbuf) == -1 || !S_ISREG(sbuf.st_mode)){
    close(fd);
    return NULL;
  }
  limit = limit > 0 ? tclmin((int)sbuf.st_size, limit) : sbuf.st_size;
  char *buf = tcmalloc(sbuf.st_size + 1);
  char *wp = buf;
  int rsiz;
  while((rsiz = read(fd, wp, limit - (wp - buf))) > 0){
    wp += rsiz;
  }
  *wp = '\0';
  close(fd);
  *sp = wp - buf;
  return buf;
}


/* Read every line of a file. */
TCLIST *tcreadfilelines(const char *path){
  int fd = path ? open(path, O_RDONLY, TC_FILEMODE) : 0;
  if(fd == -1) return NULL;
  TCLIST *list = tclistnew();
  TCXSTR *xstr = tcxstrnew();
  char buf[TC_IOBUFSIZ];
  int rsiz;
  while((rsiz = read(fd, buf, TC_IOBUFSIZ)) > 0){
    for(int i = 0; i < rsiz; i++){
      switch(buf[i]){
      case '\r':
        break;
      case '\n':
        tclistpush(list, tcxstrptr(xstr), tcxstrsize(xstr));
        tcxstrclear(xstr);
        break;
      default:
        tcxstrcat(xstr, buf + i, 1);
        break;
      }
    }
  }
  tclistpush(list, tcxstrptr(xstr), tcxstrsize(xstr));
  tcxstrdel(xstr);
  if(path) close(fd);
  return list;
}


/* Write data into a file. */
bool tcwritefile(const char *path, const void *ptr, int size){
  assert(ptr && size >= 0);
  int fd = 1;
  if(path && (fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, TC_FILEMODE)) == -1) return false;
  bool err = false;
  if(!tcwrite(fd, ptr, size)) err = true;
  if(close(fd) == -1) err = true;
  return err ? false : true;
}


/* Read names of files in a directory. */
TCLIST *tcreaddir(const char *path){
  assert(path);
  DIR *DD;
  struct dirent *dp;
  if(!(DD = opendir(path))) return NULL;
  TCLIST *list = tclistnew();
  while((dp = readdir(DD)) != NULL){
    if(!strcmp(dp->d_name, MYCDIRSTR) || !strcmp(dp->d_name, MYPDIRSTR)) continue;
    tclistpush(list, dp->d_name, strlen(dp->d_name));
  }
  closedir(DD);
  return list;
}


/* Remove a file or a directory and its sub ones recursively. */
bool tcremovelink(const char *path){
  assert(path);
  struct stat sbuf;
  if(lstat(path, &sbuf) == -1) return false;
  if(unlink(path) == 0) return true;
  TCLIST *list;
  if(!S_ISDIR(sbuf.st_mode) || !(list = tcreaddir(path))) return false;
  bool tail = path[0] != '\0' && path[strlen(path)-1] == MYPATHCHR;
  for(int i = 0; i < tclistnum(list); i++){
    const char *elem = tclistval2(list, i);
    if(!strcmp(MYCDIRSTR, elem) || !strcmp(MYPDIRSTR, elem)) continue;
    char *cpath;
    if(tail){
      cpath = tcsprintf("%s%s", path, elem);
    } else {
      cpath = tcsprintf("%s%c%s", path, MYPATHCHR, elem);
    }
    tcremovelink(cpath);
    free(cpath);
  }
  tclistdel(list);
  return rmdir(path) == 0 ? true : false;
}


/* Write data into a file. */
bool tcwrite(int fd, const void *buf, size_t size){
  assert(fd >= 0 && buf && size >= 0);
  const char *rp = buf;
  do {
    int wb = write(fd, rp, size);
    switch(wb){
    case -1: if(errno != EINTR) return false;
    case 0: break;
    default:
      rp += wb;
      size -= wb;
      break;
    }
  } while(size > 0);
  return true;
}


/* Read data from a file. */
bool tcread(int fd, void *buf, size_t size){
  assert(fd >= 0 && buf && size >= 0);
  char *wp = buf;
  do {
    int rb = read(fd, wp, size);
    switch(rb){
    case -1: if(errno != EINTR) return false;
    case 0: return size < 1;
    default:
      wp += rb;
      size -= rb;
    }
  } while(size > 0);
  return true;
}


/* Lock a file. */
bool tclock(int fd, bool ex, bool nb){
  assert(fd >= 0);
  struct flock lock;
  memset(&lock, 0, sizeof(struct flock));
  lock.l_type = ex ? F_WRLCK : F_RDLCK;
  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;
  lock.l_pid = 0;
  while(fcntl(fd, nb ? F_SETLK : F_SETLKW, &lock) == -1){
    if(errno != EINTR) return false;
  }
  return true;
}



/*************************************************************************************************
 * encoding utilities
 *************************************************************************************************/


#define TC_URLELBNUM   31                // bucket number of URL elements
#define TC_ENCBUFSIZ   32                // size of a buffer for encoding name
#define TC_XMLATBNUM   31                // bucket number of XML attributes


/* Encode a serial object with URL encoding. */
char *tcurlencode(const char *ptr, int size){
  assert(ptr && size >= 0);
  char *buf = tcmalloc(size * 3 + 1);
  char *wp = buf;
  for(int i = 0; i < size; i++){
    int c = ((unsigned char *)ptr)[i];
    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
       (c >= '0' && c <= '9') || (c != '\0' && strchr("_-.!~*'()", c))){
      *(wp++) = c;
    } else {
      wp += sprintf(wp, "%%%02X", c);
    }
  }
  *wp = '\0';
  return buf;
}


/* Decode a string encoded with URL encoding. */
char *tcurldecode(const char *str, int *sp){
  assert(str && sp);
  char *buf = tcstrdup(str);
  char *wp = buf;
  while(*str != '\0'){
    if(*str == '%'){
      str++;
      if(((str[0] >= '0' && str[0] <= '9') || (str[0] >= 'A' && str[0] <= 'F') ||
          (str[0] >= 'a' && str[0] <= 'f')) &&
         ((str[1] >= '0' && str[1] <= '9') || (str[1] >= 'A' && str[1] <= 'F') ||
          (str[1] >= 'a' && str[1] <= 'f'))){
        unsigned char c = *str;
        if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if(c >= 'a' && c <= 'z'){
          *wp = c - 'a' + 10;
        } else {
          *wp = c - '0';
        }
        *wp *= 0x10;
        str++;
        c = *str;
        if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if(c >= 'a' && c <= 'z'){
          *wp += c - 'a' + 10;
        } else {
          *wp += c - '0';
        }
        str++;
        wp++;
      } else {
        break;
      }
    } else if(*str == '+'){
      *wp = ' ';
      str++;
      wp++;
    } else {
      *wp = *str;
      str++;
      wp++;
    }
  }
  *wp = '\0';
  *sp = wp - buf;
  return buf;
}


/* Break up a URL into elements. */
TCMAP *tcurlbreak(const char *str){
  assert(str);
  TCMAP *map = tcmapnew2(TC_URLELBNUM);
  char *tmp = tcstrdup(str);
  const char *rp = tcstrtrim(tmp);
  tcmapput2(map, "self", rp);
  bool serv = false;
  if(tcstrifwm(rp, "http://")){
    tcmapput2(map, "scheme", "http");
    rp += 7;
    serv = true;
  } else if(tcstrifwm(rp, "https://")){
    tcmapput2(map, "scheme", "https");
    rp += 8;
    serv = true;
  } else if(tcstrifwm(rp, "ftp://")){
    tcmapput2(map, "scheme", "ftp");
    rp += 6;
    serv = true;
  } else if(tcstrifwm(rp, "sftp://")){
    tcmapput2(map, "scheme", "sftp");
    rp += 7;
    serv = true;
  } else if(tcstrifwm(rp, "ftps://")){
    tcmapput2(map, "scheme", "ftps");
    rp += 7;
    serv = true;
  } else if(tcstrifwm(rp, "tftp://")){
    tcmapput2(map, "scheme", "tftp");
    rp += 7;
    serv = true;
  } else if(tcstrifwm(rp, "ldap://")){
    tcmapput2(map, "scheme", "ldap");
    rp += 7;
    serv = true;
  } else if(tcstrifwm(rp, "ldaps://")){
    tcmapput2(map, "scheme", "ldaps");
    rp += 8;
    serv = true;
  } else if(tcstrifwm(rp, "file://")){
    tcmapput2(map, "scheme", "file");
    rp += 7;
    serv = true;
  }
  char *ep;
  if((ep = strchr(rp, '#')) != NULL){
    tcmapput2(map, "fragment", ep + 1);
    *ep = '\0';
  }
  if((ep = strchr(rp, '?')) != NULL){
    tcmapput2(map, "query", ep + 1);
    *ep = '\0';
  }
  if(serv){
    if((ep = strchr(rp, '/')) != NULL){
      tcmapput2(map, "path", ep);
      *ep = '\0';
    } else {
      tcmapput2(map, "path", "/");
    }
    if((ep = strchr(rp, '@')) != NULL){
      *ep = '\0';
      if(rp[0] != '\0') tcmapput2(map, "authority", rp);
      rp = ep + 1;
    }
    if((ep = strchr(rp, ':')) != NULL){
      if(ep[1] != '\0') tcmapput2(map, "port", ep + 1);
      *ep = '\0';
    }
    if(rp[0] != '\0') tcmapput2(map, "host", rp);
  } else {
    tcmapput2(map, "path", rp);
  }
  free(tmp);
  if((rp = tcmapget2(map, "path")) != NULL){
    if((ep = strrchr(rp, '/')) != NULL){
      if(ep[1] != '\0') tcmapput2(map, "file", ep + 1);
    } else {
      tcmapput2(map, "file", rp);
    }
  }
  if((rp = tcmapget2(map, "file")) != NULL && (!strcmp(rp, ".") || !strcmp(rp, "..")))
    tcmapout2(map, "file");
  return map;
}


/* Resolve a relative URL with an absolute URL. */
char *tcurlresolve(const char *base, const char *target){
  assert(base && target);
  const char *vbuf, *path;
  char *tmp, *wp, *enc;
  while(*base > '\0' && *base <= ' '){
    base++;
  }
  while(*target > '\0' && *target <= ' '){
    target++;
  }
  if(*target == '\0') target = base;
  TCXSTR *rbuf = tcxstrnew();
  TCMAP *telems = tcurlbreak(target);
  int port = 80;
  TCMAP *belems = tcurlbreak(tcmapget2(telems, "scheme") ? target : base);
  if((vbuf = tcmapget2(belems, "scheme")) != NULL){
    tcxstrcat2(rbuf, vbuf);
    tcxstrcat(rbuf, "://", 3);
    if(!tcstricmp(vbuf, "https")){
      port = 443;
    } else if(!tcstricmp(vbuf, "ftp")){
      port = 21;
    } else if(!tcstricmp(vbuf, "sftp")){
      port = 115;
    } else if(!tcstricmp(vbuf, "ftps")){
      port = 22;
    } else if(!tcstricmp(vbuf, "tftp")){
      port = 69;
    } else if(!tcstricmp(vbuf, "ldap")){
      port = 389;
    } else if(!tcstricmp(vbuf, "ldaps")){
      port = 636;
    }
  } else {
    tcxstrcat2(rbuf, "http://");
  }
  int vsiz;
  if((vbuf = tcmapget2(belems, "authority")) != NULL){
    if((wp = strchr(vbuf, ':')) != NULL){
      *wp = '\0';
      tmp = tcurldecode(vbuf, &vsiz);
      enc = tcurlencode(tmp, vsiz);
      tcxstrcat2(rbuf, enc);
      free(enc);
      free(tmp);
      tcxstrcat(rbuf, ":", 1);
      wp++;
      tmp = tcurldecode(wp, &vsiz);
      enc = tcurlencode(tmp, vsiz);
      tcxstrcat2(rbuf, enc);
      free(enc);
      free(tmp);
    } else {
      tmp = tcurldecode(vbuf, &vsiz);
      enc = tcurlencode(tmp, vsiz);
      tcxstrcat2(rbuf, enc);
      free(enc);
      free(tmp);
    }
    tcxstrcat(rbuf, "@", 1);
  }
  if((vbuf = tcmapget2(belems, "host")) != NULL){
    tmp = tcurldecode(vbuf, &vsiz);
    tcstrtolower(tmp);
    enc = tcurlencode(tmp, vsiz);
    tcxstrcat2(rbuf, enc);
    free(enc);
    free(tmp);
  } else {
    tcxstrcat(rbuf, "localhost", 9);
  }
  int num;
  char numbuf[TC_NUMBUFSIZ];
  if((vbuf = tcmapget2(belems, "port")) != NULL && (num = atoi(vbuf)) != port && num > 0){
    sprintf(numbuf, ":%d", num);
    tcxstrcat2(rbuf, numbuf);
  }
  if(!(path = tcmapget2(telems, "path"))) path = "/";
  if(path[0] == '\0' && (vbuf = tcmapget2(belems, "path")) != NULL) path = vbuf;
  if(path[0] == '\0') path = "/";
  TCLIST *bpaths = tclistnew();
  TCLIST *opaths;
  if(path[0] != '/' && (vbuf = tcmapget2(belems, "path")) != NULL){
    opaths = tcstrsplit(vbuf, "/");
  } else {
    opaths = tcstrsplit("/", "/");
  }
  free(tclistpop2(opaths));
  for(int i = 0; i < tclistnum(opaths); i++){
    vbuf = tclistval(opaths, i, &vsiz);
    if(vsiz < 1 || !strcmp(vbuf, ".")) continue;
    if(!strcmp(vbuf, "..")){
      free(tclistpop2(bpaths));
    } else {
      tclistpush(bpaths, vbuf, vsiz);
    }
  }
  tclistdel(opaths);
  opaths = tcstrsplit(path, "/");
  for(int i = 0; i < tclistnum(opaths); i++){
    vbuf = tclistval(opaths, i, &vsiz);
    if(vsiz < 1 || !strcmp(vbuf, ".")) continue;
    if(!strcmp(vbuf, "..")){
      free(tclistpop2(bpaths));
    } else {
      tclistpush(bpaths, vbuf, vsiz);
    }
  }
  tclistdel(opaths);
  for(int i = 0; i < tclistnum(bpaths); i++){
    vbuf = tclistval2(bpaths, i);
    if(strchr(vbuf, '%')){
      tmp = tcurldecode(vbuf, &vsiz);
    } else {
      tmp = tcstrdup(vbuf);
    }
    enc = tcurlencode(tmp, strlen(tmp));
    tcxstrcat(rbuf, "/", 1);
    tcxstrcat2(rbuf, enc);
    free(enc);
    free(tmp);
  }
  if(tcstrbwm(path, "/")) tcxstrcat(rbuf, "/", 1);
  tclistdel(bpaths);
  if((vbuf = tcmapget2(telems, "query")) != NULL ||
     (*target == '#' && (vbuf = tcmapget2(belems, "query")) != NULL)){
    tcxstrcat(rbuf, "?", 1);
    TCLIST *qelems = tcstrsplit(vbuf, "&;");
    for(int i = 0; i < tclistnum(qelems); i++){
      vbuf = tclistval2(qelems, i);
      if(i > 0) tcxstrcat(rbuf, "&", 1);
      if((wp = strchr(vbuf, '=')) != NULL){
        *wp = '\0';
        tmp = tcurldecode(vbuf, &vsiz);
        enc = tcurlencode(tmp, vsiz);
        tcxstrcat2(rbuf, enc);
        free(enc);
        free(tmp);
        tcxstrcat(rbuf, "=", 1);
        wp++;
        tmp = tcurldecode(wp, &vsiz);
        enc = tcurlencode(tmp, strlen(tmp));
        tcxstrcat2(rbuf, enc);
        free(enc);
        free(tmp);
      } else {
        tmp = tcurldecode(vbuf, &vsiz);
        enc = tcurlencode(tmp, vsiz);
        tcxstrcat2(rbuf, enc);
        free(enc);
        free(tmp);
      }
    }
    tclistdel(qelems);
  }
  if((vbuf = tcmapget2(telems, "fragment")) != NULL){
    tmp = tcurldecode(vbuf, &vsiz);
    enc = tcurlencode(tmp, vsiz);
    tcxstrcat(rbuf, "#", 1);
    tcxstrcat2(rbuf, enc);
    free(enc);
    free(tmp);
  }
  tcmapdel(belems);
  tcmapdel(telems);
  return tcxstrtomalloc(rbuf);
}


/* Encode a serial object with Base64 encoding. */
char *tcbaseencode(const char *ptr, int size){
  assert(ptr && size >= 0);
  char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const unsigned char *obj = (const unsigned char *)ptr;
  char *buf = tcmalloc(4 * (size + 2) / 3 + 1);
  char *wp = buf;
  for(int i = 0; i < size; i += 3){
    switch(size - i){
    case 1:
      *wp++ = tbl[obj[0] >> 2];
      *wp++ = tbl[(obj[0] & 3) << 4];
      *wp++ = '=';
      *wp++ = '=';
      break;
    case 2:
      *wp++ = tbl[obj[0] >> 2];
      *wp++ = tbl[((obj[0] & 3) << 4) + (obj[1] >> 4)];
      *wp++ = tbl[(obj[1] & 0xf) << 2];
      *wp++ = '=';
      break;
    default:
      *wp++ = tbl[obj[0] >> 2];
      *wp++ = tbl[((obj[0] & 3) << 4) + (obj[1] >> 4)];
      *wp++ = tbl[((obj[1] & 0xf) << 2) + (obj[2] >> 6)];
      *wp++ = tbl[obj[2] & 0x3f];
      break;
    }
    obj += 3;
  }
  *wp = '\0';
  return buf;
}


/* Decode a string encoded with Base64 encoding. */
char *tcbasedecode(const char *str, int *sp){
  assert(str && sp);
  int cnt = 0;
  int bpos = 0;
  int eqcnt = 0;
  int len = strlen(str);
  unsigned char *obj = tcmalloc(len + 4);
  unsigned char *wp = obj;
  while(bpos < len && eqcnt == 0){
    int bits = 0;
    int i;
    for(i = 0; bpos < len && i < 4; bpos++){
      if(str[bpos] >= 'A' && str[bpos] <= 'Z'){
        bits = (bits << 6) | (str[bpos] - 'A');
        i++;
      } else if(str[bpos] >= 'a' && str[bpos] <= 'z'){
        bits = (bits << 6) | (str[bpos] - 'a' + 26);
        i++;
      } else if(str[bpos] >= '0' && str[bpos] <= '9'){
        bits = (bits << 6) | (str[bpos] - '0' + 52);
        i++;
      } else if(str[bpos] == '+'){
        bits = (bits << 6) | 62;
        i++;
      } else if(str[bpos] == '/'){
        bits = (bits << 6) | 63;
        i++;
      } else if(str[bpos] == '='){
        bits <<= 6;
        i++;
        eqcnt++;
      }
    }
    if(i == 0 && bpos >= len) continue;
    switch(eqcnt){
    case 0:
      *wp++ = (bits >> 16) & 0xff;
      *wp++ = (bits >> 8) & 0xff;
      *wp++ = bits & 0xff;
      cnt += 3;
      break;
    case 1:
      *wp++ = (bits >> 16) & 0xff;
      *wp++ = (bits >> 8) & 0xff;
      cnt += 2;
      break;
    case 2:
      *wp++ = (bits >> 16) & 0xff;
      cnt += 1;
      break;
    }
  }
  obj[cnt] = '\0';
  *sp = cnt;
  return (char *)obj;
}


/* Encode a serial object with Quoted-printable encoding. */
char *tcquoteencode(const char *ptr, int size){
  assert(ptr && size >= 0);
  const unsigned char *rp = (const unsigned char *)ptr;
  char *buf = tcmalloc(size * 3 + 1);
  char *wp = buf;
  int cols = 0;
  for(int i = 0; i < size; i++){
    if(rp[i] == '=' || (rp[i] < 0x20 && rp[i] != '\r' && rp[i] != '\n' && rp[i] != '\t') ||
       rp[i] > 0x7e){
      wp += sprintf(wp, "=%02X", rp[i]);
      cols += 3;
    } else {
      *(wp++) = rp[i];
      cols++;
    }
  }
  *wp = '\0';
  return buf;
}


/* Decode a string encoded with Quoted-printable encoding. */
char *tcquotedecode(const char *str, int *sp){
  assert(str && sp);
  char *buf = tcmalloc(strlen(str) + 1);
  char *wp = buf;
  for(; *str != '\0'; str++){
    if(*str == '='){
      str++;
      if(*str == '\0'){
        break;
      } else if(str[0] == '\r' && str[1] == '\n'){
        str++;
      } else if(str[0] != '\n' && str[0] != '\r'){
        if(*str >= 'A' && *str <= 'Z'){
          *wp = (*str - 'A' + 10) * 16;
        } else if(*str >= 'a' && *str <= 'z'){
          *wp = (*str - 'a' + 10) * 16;
        } else {
          *wp = (*str - '0') * 16;
        }
        str++;
        if(*str == '\0') break;
        if(*str >= 'A' && *str <= 'Z'){
          *wp += *str - 'A' + 10;
        } else if(*str >= 'a' && *str <= 'z'){
          *wp += *str - 'a' + 10;
        } else {
          *wp += *str - '0';
        }
        wp++;
      }
    } else {
      *wp = *str;
      wp++;
    }
  }
  *wp = '\0';
  *sp = wp - buf;
  return buf;
}


/* Encode a string with MIME encoding. */
char *tcmimeencode(const char *str, const char *encname, bool base){
  assert(str && encname);
  int len = strlen(str);
  char *buf = tcmalloc(len * 3 + strlen(encname) + 16);
  char *wp = buf;
  wp += sprintf(wp, "=?%s?%c?", encname, base ? 'B' : 'Q');
  char *enc = base ? tcbaseencode(str, len) : tcquoteencode(str, len);
  wp += sprintf(wp, "%s?=", enc);
  free(enc);
  return buf;
}


/* Decode a string encoded with MIME encoding. */
char *tcmimedecode(const char *str, char *enp){
  assert(str);
  if(enp) sprintf(enp, "US-ASCII");
  char *buf = tcmalloc(strlen(str) + 1);
  char *wp = buf;
  while(*str != '\0'){
    if(tcstrfwm(str, "=?")){
      str += 2;
      const char *pv = str;
      const char *ep = strchr(str, '?');
      if(!ep) continue;
      if(enp && ep - pv < TC_ENCBUFSIZ){
        memcpy(enp, pv, ep - pv);
        enp[ep-pv] = '\0';
      }
      pv = ep + 1;
      bool quoted = (*pv == 'Q' || *pv == 'q');
      if(*pv != '\0') pv++;
      if(*pv != '\0') pv++;
      if(!(ep = strchr(pv, '?'))) continue;
      char *tmp = tcmemdup(pv, ep - pv);
      int len;
      char *dec = quoted ? tcquotedecode(tmp, &len) : tcbasedecode(tmp, &len);
      wp += sprintf(wp, "%s", dec);
      free(dec);
      free(tmp);
      str = ep + 1;
      if(*str != '\0') str++;
    } else {
      *(wp++) = *str;
      str++;
    }
  }
  *wp = '\0';
  return buf;
}


/* Compress a serial object with Packbits encoding. */
char *tcpackencode(const char *ptr, int size, int *sp){
  assert(ptr && size >= 0 && sp);
  char *buf = tcmalloc(size * 2 + 1);
  char *wp = buf;
  const char *end = ptr + size;
  while(ptr < end){
    char *sp = wp;
    const char *rp = ptr + 1;
    int step = 1;
    while(rp < end && step < 0x7f && *rp == *ptr){
      step++;
      rp++;
    }
    if(step <= 1 && rp < end){
      wp = sp + 1;
      *(wp++) = *ptr;
      while(rp < end && step < 0x7f && *rp != *(rp - 1)){
        *(wp++) = *rp;
        step++;
        rp++;
      }
      if(rp < end && *(rp - 1) == *rp){
        wp--;
        rp--;
        step--;
      }
      *sp = step == 1 ? 1 : -step;
    } else {
      *(wp++) = step;
      *(wp++) = *ptr;
    }
    ptr += step;
  }
  *sp = wp - buf;
  return buf;
}


/* Decompress a serial object compressed with Packbits encoding. */
char *tcpackdecode(const char *ptr, int size, int *sp){
  assert(ptr && size >= 0 && sp);
  int asiz = size * 3;
  char *buf = tcmalloc(asiz + 1);
  int wi = 0;
  const char *end = ptr + size;
  while(ptr < end){
    int step = abs(*ptr);
    if(wi + step >= asiz){
      asiz = asiz * 2 + step;
      buf = tcrealloc(buf, asiz + 1);
    }
    if(*(ptr++) >= 0){
      memset(buf + wi, *ptr, step);
      ptr++;
    } else {
      step = tclmin(step, end - ptr);
      memcpy(buf + wi, ptr, step);
      ptr += step;
    }
    wi += step;
  }
  buf[wi] = '\0';
  *sp = wi;
  return buf;
}


/* Compress a serial object with Deflate encoding. */
char *tcdeflate(const char *ptr, int size, int *sp){
  assert(ptr && sp);
  if(!_tc_deflate) return NULL;
  return _tc_deflate(ptr, size, sp, _TC_ZMZLIB);
}


/* Decompress a serial object compressed with Deflate encoding. */
char *tcinflate(const char *ptr, int size, int *sp){
  assert(ptr && size >= 0);
  if(!_tc_inflate) return NULL;
  return _tc_inflate(ptr, size, sp, _TC_ZMZLIB);
}


/* Compress a serial object with GZIP encoding. */
char *tcgzipencode(const char *ptr, int size, int *sp){
  assert(ptr && sp);
  if(!_tc_deflate) return NULL;
  return _tc_deflate(ptr, size, sp, _TC_ZMGZIP);
}


/* Decompress a serial object compressed with GZIP encoding. */
char *tcgzipdecode(const char *ptr, int size, int *sp){
  assert(ptr && size >= 0);
  if(!_tc_inflate) return NULL;
  return _tc_inflate(ptr, size, sp, _TC_ZMGZIP);
}


/* Get the CRC32 checksum of a serial object. */
unsigned int tcgetcrc(const char *ptr, int size){
  assert(ptr && size >= 0);
  if(!_tc_getcrc) return 0;
  return _tc_getcrc(ptr, size);
}


/* Escape meta characters in a string with the entity references of XML. */
char *tcxmlescape(const char *str){
  assert(str);
  const char *rp = str;
  int bsiz = 0;
  while(*rp != '\0'){
    switch(*rp){
    case '&':
      bsiz += 5;
      break;
    case '<':
      bsiz += 4;
      break;
    case '>':
      bsiz += 4;
      break;
    case '"':
      bsiz += 6;
      break;
    default:
      bsiz++;
      break;
    }
    rp++;
  }
  char *buf = tcmalloc(bsiz + 1);
  char *wp = buf;
  while(*str != '\0'){
    switch(*str){
    case '&':
      memcpy(wp, "&amp;", 5);
      wp += 5;
      break;
    case '<':
      memcpy(wp, "&lt;", 4);
      wp += 4;
      break;
    case '>':
      memcpy(wp, "&gt;", 4);
      wp += 4;
      break;
    case '"':
      memcpy(wp, "&quot;", 6);
      wp += 6;
      break;
    default:
      *(wp++) = *str;
      break;
    }
    str++;
  }
  *wp = '\0';
  return buf;
}


/* Unescape entity references in a string of XML. */
char *tcxmlunescape(const char *str){
  assert(str);
  char *buf = tcmalloc(strlen(str) + 1);
  char *wp = buf;
  while(*str != '\0'){
    if(*str == '&'){
      if(tcstrfwm(str, "&amp;")){
        *(wp++) = '&';
        str += 5;
      } else if(tcstrfwm(str, "&lt;")){
        *(wp++) = '<';
        str += 4;
      } else if(tcstrfwm(str, "&gt;")){
        *(wp++) = '>';
        str += 4;
      } else if(tcstrfwm(str, "&quot;")){
        *(wp++) = '"';
        str += 6;
      } else {
        *(wp++) = *(str++);
      }
    } else {
      *(wp++) = *(str++);
    }
  }
  *wp = '\0';
  return buf;
}


/* Split an XML string into tags and text sections. */
TCLIST *tcxmlbreak(const char *str){
  assert(str);
  TCLIST *list = tclistnew();
  int i = 0;
  int pv = 0;
  bool tag = false;
  char *ep;
  while(true){
    if(str[i] == '\0'){
      if(i > pv) tclistpush(list, str + pv, i - pv);
      break;
    } else if(!tag && str[i] == '<'){
      if(str[i+1] == '!' && str[i+2] == '-' && str[i+3] == '-'){
        if(i > pv) tclistpush(list, str + pv, i - pv);
        if((ep = strstr(str + i, "-->")) != NULL){
          tclistpush(list, str + i, ep - str - i + 3);
          i = ep - str + 2;
          pv = i + 1;
        }
      } else if(str[i+1] == '!' && str[i+2] == '[' && tcstrifwm(str + i, "<![CDATA[")){
        if(i > pv) tclistpush(list, str + pv, i - pv);
        if((ep = strstr(str + i, "]]>")) != NULL){
          i += 9;
          TCXSTR *xstr = tcxstrnew();
          while(str + i < ep){
            if(str[i] == '&'){
              tcxstrcat(xstr, "&amp;", 5);
            } else if(str[i] == '<'){
              tcxstrcat(xstr, "&lt;", 4);
            } else if(str[i] == '>'){
              tcxstrcat(xstr, "&gt;", 4);
            } else {
              tcxstrcat(xstr, str + i, 1);
            }
            i++;
          }
          if(tcxstrsize(xstr) > 0) tclistpush(list, tcxstrptr(xstr), tcxstrsize(xstr));
          tcxstrdel(xstr);
          i = ep - str + 2;
          pv = i + 1;
        }
      } else {
        if(i > pv) tclistpush(list, str + pv, i - pv);
        tag = true;
        pv = i;
      }
    } else if(tag && str[i] == '>'){
      if(i > pv) tclistpush(list, str + pv, i - pv + 1);
      tag = false;
      pv = i + 1;
    }
    i++;
  }
  return list;
}


/* Get the map of attributes of an XML tag. */
TCMAP *tcxmlattrs(const char *str){
  assert(str);
  TCMAP *map = tcmapnew2(TC_XMLATBNUM);
  const unsigned char *rp = (unsigned char *)str;
  while(*rp == '<' || *rp == '/' || *rp == '?' || *rp == '!' || *rp == ' '){
    rp++;
  }
  const unsigned char *key = rp;
  while(*rp > 0x20 && *rp != '/' && *rp != '>'){
    rp++;
  }
  tcmapputkeep(map, "", 0, (char *)key, rp - key);
  while(*rp != '\0'){
    while(*rp != '\0' && (*rp <= 0x20 || *rp == '/' || *rp == '?' || *rp == '>')){
      rp++;
    }
    key = rp;
    while(*rp > 0x20 && *rp != '/' && *rp != '>' && *rp != '='){
      rp++;
    }
    int ksiz = rp - key;
    while(*rp != '\0' && (*rp == '=' || *rp <= 0x20)){
      rp++;
    }
    const unsigned char *val;
    int vsiz;
    if(*rp == '"'){
      rp++;
      val = rp;
      while(*rp != '\0' && *rp != '"'){
        rp++;
      }
      vsiz = rp - val;
    } else if(*rp == '\''){
      rp++;
      val = rp;
      while(*rp != '\0' && *rp != '\''){
        rp++;
      }
      vsiz = rp - val;
    } else {
      val = rp;
      while(*rp > 0x20 && *rp != '"' && *rp != '\'' && *rp != '>'){
        rp++;
      }
      vsiz = rp - val;
    }
    if(*rp != '\0') rp++;
    if(ksiz > 0){
      char *copy = tcmemdup(val, vsiz);
      char *raw = tcxmlunescape(copy);
      tcmapputkeep(map, (char *)key, ksiz, raw, strlen(raw));
      free(raw);
      free(copy);
    }
  }
  return map;
}



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


#define TC_BSENCUNIT    8192             // unit size of TCBS encoding
#define TC_BWTCNTMIN    64               // minimum element number of counting sort
#define TC_BWTCNTLV     4                // maximum recursion level of counting sort
#define TC_BWTBUFNUM    16384            // number of elements of BWT buffer

typedef struct {                         // type of structure for a BWT character
  int fchr;                              // character code of the first character
  int tchr;                              // character code of the last character
} TCBWTREC;


/* private function prototypes */
static void tcbwtsortstrcount(const char **arrays, int anum, int len, int level);
static void tcbwtsortstrinsert(const char **arrays, int anum, int len, int skip);
static void tcbwtsortstrheap(const char **arrays, int anum, int len, int skip);
static void tcbwtsortchrcount(unsigned char *str, int len);
static void tcbwtsortchrinsert(unsigned char *str, int len);
static void tcbwtsortreccount(TCBWTREC *arrays, int anum);
static void tcbwtsortrecinsert(TCBWTREC *array, int anum);
static int tcbwtsearchrec(TCBWTREC *array, int anum, int tchr);
static void tcmtfencode(char *ptr, int size);
static void tcmtfdecode(char *ptr, int size);
static int tcgammaencode(const char *ptr, int size, char *obuf);
static int tcgammadecode(const char *ptr, int size, char *obuf);


/* Show error message on the standard error output and exit. */
void *tcmyfatal(const char *message){
  assert(message);
  if(tcfatalfunc){
    tcfatalfunc(message);
  } else {
    fprintf(stderr, "fatal error: %s\n", message);
  }
  exit(1);
  return NULL;
}


/* Global mutex object. */
static pthread_mutex_t tcglobalmutex = PTHREAD_MUTEX_INITIALIZER;


/* Lock the global mutex object. */
bool tcglobalmutexlock(void){
  if(!TCUSEPTHREAD) memset(&tcglobalmutex, 0, sizeof(tcglobalmutex));
  return pthread_mutex_lock(&tcglobalmutex) == 0;
}


/* Unlock the global mutex object. */
bool tcglobalmutexunlock(void){
  return pthread_mutex_unlock(&tcglobalmutex) == 0;
}


/* Compress a serial object with TCBS encoding. */
char *tcbsencode(const char *ptr, int size, int *sp){
  assert(ptr && size >= 0 && sp);
  char *result = tcmalloc((size * 7) / 3 + (size / TC_BSENCUNIT + 1) * sizeof(uint16_t) +
                          TC_BSENCUNIT * 2 + 0x200);
  char *pv = result + size + 0x100;
  char *wp = pv;
  char *tp = pv + size + 0x100;
  const char *end = ptr + size;
  while(ptr < end){
    int usiz = tclmin(TC_BSENCUNIT, end - ptr);
    memcpy(tp, ptr, usiz);
    memcpy(tp + usiz, ptr, usiz);
    char *sp = wp;
    uint16_t idx = 0;
    wp += sizeof(idx);
    const char *arrays[usiz+1];
    for(int i = 0; i < usiz; i++){
      arrays[i] = tp + i;
    }
    const char *fp = arrays[0];
    if(usiz >= TC_BWTCNTMIN){
      tcbwtsortstrcount(arrays, usiz, usiz, 0);
    } else if(usiz > 1){
      tcbwtsortstrinsert(arrays, usiz, usiz, 0);
    }
    for(int i = 0; i < usiz; i++){
      int tidx = arrays[i] - fp;
      if(tidx == 0){
        idx = i;
        *(wp++) = ptr[usiz-1];
      } else {
        *(wp++) = ptr[tidx-1];
      }
    }
    idx = TCHTOIS(idx);
    memcpy(sp, &idx, sizeof(idx));
    ptr += TC_BSENCUNIT;
  }
  size = wp - pv;
  tcmtfencode(pv, size);
  int nsiz = tcgammaencode(pv, size, result);
  *sp = nsiz;
  return result;
}


/* Decompress a serial object compressed with TCBS encoding. */
char *tcbsdecode(const char *ptr, int size, int *sp){
  char *result = tcmalloc(size * 9 + 0x200);
  char *wp = result + size + 0x100;
  int nsiz = tcgammadecode(ptr, size, wp);
  tcmtfdecode(wp, nsiz);
  ptr = wp;
  wp = result;
  const char *end = ptr + nsiz;
  while(ptr < end){
    uint16_t idx;
    memcpy(&idx, ptr, sizeof(idx));
    idx = TCITOHS(idx);
    ptr += sizeof(idx);
    int usiz = tclmin(TC_BSENCUNIT, end - ptr);
    if(idx >= usiz) idx = 0;
    char rbuf[usiz+1];
    memcpy(rbuf, ptr, usiz);
    if(usiz >= TC_BWTCNTMIN){
      tcbwtsortchrcount((unsigned char *)rbuf, usiz);
    } else if(usiz > 0){
      tcbwtsortchrinsert((unsigned char *)rbuf, usiz);
    }
    int fnums[0x100], tnums[0x100];
    memset(fnums, 0, sizeof(fnums));
    memset(tnums, 0, sizeof(tnums));
    TCBWTREC array[usiz+1];
    TCBWTREC *rp = array;
    for(int i = 0; i < usiz; i++){
      int fc = *(unsigned char *)(rbuf + i);
      rp->fchr = (fc << 23) + fnums[fc]++;
      int tc = *(unsigned char *)(ptr + i);
      rp->tchr = (tc << 23) + tnums[tc]++;
      rp++;
    }
    unsigned int fchr = array[idx].fchr;
    if(usiz >= TC_BWTCNTMIN){
      tcbwtsortreccount(array, usiz);
    } else if(usiz > 1){
      tcbwtsortrecinsert(array, usiz);
    }
    for(int i = 0; i < usiz; i++){
      if(array[i].fchr == fchr){
        idx = i;
        break;
      }
    }
    for(int i = 0; i < usiz; i++){
      *(wp++) = array[idx].fchr >> 23;
      idx = tcbwtsearchrec(array, usiz, array[idx].fchr);
    }
    ptr += usiz;
  }
  *wp = '\0';
  *sp = wp - result;
  return result;
}


/* Encode a serial object with BWT encoding. */
char *tcbwtencode(const char *ptr, int size, int *idxp){
  assert(ptr && size >= 0 && idxp);
  if(size < 1){
    *idxp = 0;
    return tcmemdup("", 0);
  }
  char *result = tcmalloc(size * 3 + 1);
  char *tp = result + size + 1;
  memcpy(tp, ptr, size);
  memcpy(tp + size, ptr, size);
  const char *abuf[TC_BWTBUFNUM];
  const char **arrays = abuf;
  if(size > TC_BWTBUFNUM) arrays = tcmalloc(sizeof(*arrays) * size);
  for(int i = 0; i < size; i++){
    arrays[i] = tp + i;
  }
  const char *fp = arrays[0];
  if(size >= TC_BWTCNTMIN){
    tcbwtsortstrcount(arrays, size, size, -1);
  } else if(size > 1){
    tcbwtsortstrinsert(arrays, size, size, 0);
  }
  for(int i = 0; i < size; i++){
    int idx = arrays[i] - fp;
    if(idx == 0){
      *idxp = i;
      result[i] = ptr[size-1];
    } else {
      result[i] = ptr[idx-1];
    }
  }
  if(arrays != abuf) free(arrays);
  result[size] = '\0';
  return result;
}


/* Decode a serial object encoded with BWT encoding. */
char *tcbwtdecode(const char *ptr, int size, int idx){
  assert(ptr && size >= 0);
  if(size < 1 || idx < 0) return tcmemdup("", 0);
  if(idx >= size) idx = 0;
  char *result = tcmalloc(size + 1);
  memcpy(result, ptr, size);
  if(size >= TC_BWTCNTMIN){
    tcbwtsortchrcount((unsigned char *)result, size);
  } else {
    tcbwtsortchrinsert((unsigned char *)result, size);
  }
  int fnums[0x100], tnums[0x100];
  memset(fnums, 0, sizeof(fnums));
  memset(tnums, 0, sizeof(tnums));
  TCBWTREC abuf[TC_BWTBUFNUM];
  TCBWTREC *array = abuf;
  if(size > TC_BWTBUFNUM) array = tcmalloc(sizeof(*array) * size);
  TCBWTREC *rp = array;
  for(int i = 0; i < size; i++){
    int fc = *(unsigned char *)(result + i);
    rp->fchr = (fc << 23) + fnums[fc]++;
    int tc = *(unsigned char *)(ptr + i);
    rp->tchr = (tc << 23) + tnums[tc]++;
    rp++;
  }
  unsigned int fchr = array[idx].fchr;
  if(size >= TC_BWTCNTMIN){
    tcbwtsortreccount(array, size);
  } else if(size > 1){
    tcbwtsortrecinsert(array, size);
  }
  for(int i = 0; i < size; i++){
    if(array[i].fchr == fchr){
      idx = i;
      break;
    }
  }
  char *wp = result;
  for(int i = 0; i < size; i++){
    *(wp++) = array[idx].fchr >> 23;
    idx = tcbwtsearchrec(array, size, array[idx].fchr);
  }
  *wp = '\0';
  if(array != abuf) free(array);
  return result;
}


/* Sort BWT string arrays by dicrionary order by counting sort.
   `array' specifies an array of string arrays.
   `anum' specifies the number of the array.
   `len' specifies the size of each string.
   `level' specifies the level of recursion. */
static void tcbwtsortstrcount(const char **arrays, int anum, int len, int level){
  assert(arrays && anum >= 0 && len >= 0);
  const char *nbuf[TC_BWTBUFNUM];
  const char **narrays = nbuf;
  if(anum > TC_BWTBUFNUM) narrays = tcmalloc(sizeof(*narrays) * anum);
  int count[0x100], accum[0x100];
  memset(count, 0, sizeof(count));
  int skip = level < 0 ? 0 : level;
  for(int i = 0; i < anum; i++){
    count[((unsigned char *)arrays[i])[skip]]++;
  }
  memcpy(accum, count, sizeof(count));
  for(int i = 1; i < 0x100; i++){
    accum[i] = accum[i-1] + accum[i];
  }
  for(int i = 0; i < anum; i++){
    narrays[--accum[((unsigned char *)arrays[i])[skip]]] = arrays[i];
  }
  int off = 0;
  if(level >= 0 && level < TC_BWTCNTLV){
    for(int i = 0; i < 0x100; i++){
      int c = count[i];
      if(c > 1){
        if(c >= TC_BWTCNTMIN){
          tcbwtsortstrcount(narrays + off, c, len, level + 1);
        } else {
          tcbwtsortstrinsert(narrays + off, c, len, skip + 1);
        }
      }
      off += c;
    }
  } else {
    for(int i = 0; i < 0x100; i++){
      int c = count[i];
      if(c > 1){
        if(c >= TC_BWTCNTMIN){
          tcbwtsortstrheap(narrays + off, c, len, skip + 1);
        } else {
          tcbwtsortstrinsert(narrays + off, c, len, skip + 1);
        }
      }
      off += c;
    }
  }
  memcpy(arrays, narrays, anum * sizeof(*narrays));
  if(narrays != nbuf) free(narrays);
}


/* Sort BWT string arrays by dicrionary order by insertion sort.
   `array' specifies an array of string arrays.
   `anum' specifies the number of the array.
   `len' specifies the size of each string.
   `skip' specifies the number of skipped bytes. */
static void tcbwtsortstrinsert(const char **arrays, int anum, int len, int skip){
  assert(arrays && anum >= 0 && len >= 0);
  for(int i = 1; i < anum; i++){
    int cmp = 0;
    const unsigned char *ap = (unsigned char *)arrays[i-1];
    const unsigned char *bp = (unsigned char *)arrays[i];
    for(int j = skip; j < len; j++){
      if(ap[j] != bp[j]){
        cmp = ap[j] - bp[j];
        break;
      }
    }
    if(cmp > 0){
      const char *swap = arrays[i];
      int j;
      for(j = i; j > 0; j--){
        int cmp = 0;
        const unsigned char *ap = (unsigned char *)arrays[j-1];
        const unsigned char *bp = (unsigned char *)swap;
        for(int k = skip; k < len; k++){
          if(ap[k] != bp[k]){
            cmp = ap[k] - bp[k];
            break;
          }
        }
        if(cmp < 0) break;
        arrays[j] = arrays[j-1];
      }
      arrays[j] = swap;
    }
  }
}


/* Sort BWT string arrays by dicrionary order by heap sort.
   `array' specifies an array of string arrays.
   `anum' specifies the number of the array.
   `len' specifies the size of each string.
   `skip' specifies the number of skipped bytes. */
static void tcbwtsortstrheap(const char **arrays, int anum, int len, int skip){
  assert(arrays && anum >= 0 && len >= 0);
  anum--;
  int bottom = (anum >> 1) + 1;
  int top = anum;
  while(bottom > 0){
    bottom--;
    int mybot = bottom;
    int i = mybot << 1;
    while(i <= top){
      if(i < top){
        int cmp = 0;
        const unsigned char *ap = (unsigned char *)arrays[i+1];
        const unsigned char *bp = (unsigned char *)arrays[i];
        for(int j = skip; j < len; j++){
          if(ap[j] != bp[j]){
            cmp = ap[j] - bp[j];
            break;
          }
        }
        if(cmp > 0) i++;
      }
      int cmp = 0;
      const unsigned char *ap = (unsigned char *)arrays[mybot];
      const unsigned char *bp = (unsigned char *)arrays[i];
      for(int j = skip; j < len; j++){
        if(ap[j] != bp[j]){
          cmp = ap[j] - bp[j];
          break;
        }
      }
      if(cmp >= 0) break;
      const char *swap = arrays[mybot];
      arrays[mybot] = arrays[i];
      arrays[i] = swap;
      mybot = i;
      i = mybot << 1;
    }
  }
  while(top > 0){
    const char *swap = arrays[0];
    arrays[0] = arrays[top];
    arrays[top] = swap;
    top--;
    int mybot = bottom;
    int i = mybot << 1;
    while(i <= top){
      if(i < top){
        int cmp = 0;
        const unsigned char *ap = (unsigned char *)arrays[i+1];
        const unsigned char *bp = (unsigned char *)arrays[i];
        for(int j = 0; j < len; j++){
          if(ap[j] != bp[j]){
            cmp = ap[j] - bp[j];
            break;
          }
        }
        if(cmp > 0) i++;
      }
      int cmp = 0;
      const unsigned char *ap = (unsigned char *)arrays[mybot];
      const unsigned char *bp = (unsigned char *)arrays[i];
      for(int j = 0; j < len; j++){
        if(ap[j] != bp[j]){
          cmp = ap[j] - bp[j];
          break;
        }
      }
      if(cmp >= 0) break;
      swap = arrays[mybot];
      arrays[mybot] = arrays[i];
      arrays[i] = swap;
      mybot = i;
      i = mybot << 1;
    }
  }
}


/* Sort BWT characters by code number by counting sort.
   `str' specifies a string.
   `len' specifies the length of the string. */
static void tcbwtsortchrcount(unsigned char *str, int len){
  assert(str && len >= 0);
  unsigned char nbuf[TC_BWTBUFNUM];
  unsigned char *nstr = nbuf;
  if(len > TC_BWTBUFNUM) nstr = tcmalloc(sizeof(*nstr) * len);
  int count[0x100], accum[0x100];
  memset(count, 0, sizeof(count));
  for(int i = 0; i < len; i++){
    count[str[i]]++;
  }
  memcpy(accum, count, sizeof(count));
  for(int i = 1; i < 0x100; i++){
    accum[i] = accum[i-1] + accum[i];
  }
  for(int i = 0; i < 0x100; i++){
    accum[i] -= count[i];
  }
  for(int i = 0; i < len; i++){
    nstr[accum[str[i]]++] = str[i];
  }
  memcpy(str, nstr, len * sizeof(*nstr));
  if(nstr != nbuf) free(nstr);
}


/* Sort BWT characters by code number by insertion sort.
   `str' specifies a string.
   `len' specifies the length of the string. */
static void tcbwtsortchrinsert(unsigned char *str, int len){
  assert(str && len >= 0);
  for(int i = 1; i < len; i++){
    if(str[i-1] - str[i] > 0){
      unsigned char swap = str[i];
      int j;
      for(j = i; j > 0; j--){
        if(str[j-1] - swap < 0) break;
        str[j] = str[j-1];
      }
      str[j] = swap;
    }
  }
}


/* Sort BWT records by code number by counting sort.
   `array' specifies an array of records.
   `anum' specifies the number of the array. */
static void tcbwtsortreccount(TCBWTREC *array, int anum){
  assert(array && anum >= 0);
  TCBWTREC nbuf[TC_BWTBUFNUM];
  TCBWTREC *narray = nbuf;
  if(anum > TC_BWTBUFNUM) narray = tcmalloc(sizeof(*narray) * anum);
  int count[0x100], accum[0x100];
  memset(count, 0, sizeof(count));
  for(int i = 0; i < anum; i++){
    count[array[i].tchr>>23]++;
  }
  memcpy(accum, count, sizeof(count));
  for(int i = 1; i < 0x100; i++){
    accum[i] = accum[i-1] + accum[i];
  }
  for(int i = 0; i < 0x100; i++){
    accum[i] -= count[i];
  }
  for(int i = 0; i < anum; i++){
    narray[accum[array[i].tchr>>23]++] = array[i];
  }
  memcpy(array, narray, anum * sizeof(*narray));
  if(narray != nbuf) free(narray);
}


/* Sort BWT records by code number by insertion sort.
   `array' specifies an array of records..
   `anum' specifies the number of the array. */
static void tcbwtsortrecinsert(TCBWTREC *array, int anum){
  assert(array && anum >= 0);
  for(int i = 1; i < anum; i++){
    if(array[i-1].tchr - array[i].tchr > 0){
      TCBWTREC swap = array[i];
      int j;
      for(j = i; j > 0; j--){
        if(array[j-1].tchr - swap.tchr < 0) break;
        array[j] = array[j-1];
      }
      array[j] = swap;
    }
  }
}


/* Search the element of BWT records.
   `array' specifies an array of records.
   `anum' specifies the number of the array.
   `tchr' specifies the last code number. */
static int tcbwtsearchrec(TCBWTREC *array, int anum, int tchr){
  assert(array && anum >= 0);
  int bottom = 0;
  int top = anum;
  int mid;
  do {
    mid = (bottom + top) >> 1;
    if(array[mid].tchr == tchr){
      return mid;
    } else if(array[mid].tchr < tchr){
      bottom = mid + 1;
      if(bottom >= anum) break;
    } else {
      top = mid - 1;
    }
  } while(bottom <= top);
  return -1;
}


/* Initialization table for MTF encoder. */
const unsigned char tcmtftable[] = {
  0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
  0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
  0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
  0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
  0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
  0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
  0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
  0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
  0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
  0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
  0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
  0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
  0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
  0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
  0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
  0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff
};


/* Encode a region with MTF encoding.
   `ptr' specifies the pointer to the region.
   `size' specifies the size of the region. */
static void tcmtfencode(char *ptr, int size){
  unsigned char table1[0x100], table2[0x100], *table, *another;
  assert(ptr && size >= 0);
  memcpy(table1, tcmtftable, sizeof(tcmtftable));
  table = table1;
  another = table2;
  const char *end = ptr + size;
  char *wp = ptr;
  while(ptr < end){
    unsigned char c = *ptr;
    unsigned char *tp = table;
    unsigned char *tend = table + 0x100;
    while(tp < tend && *tp != c){
      tp++;
    }
    int idx = tp - table;
    *(wp++) = idx;
    if(idx > 0){
      memcpy(another, &c, 1);
      memcpy(another + 1, table, idx);
      memcpy(another + 1 + idx, table + idx + 1, 255 - idx);
      unsigned char *swap = table;
      table = another;
      another = swap;
    }
    ptr++;
  }
}


/* Decode a region compressed with MTF encoding.
   `ptr' specifies the pointer to the region.
   `size' specifies the size of the region. */
static void tcmtfdecode(char *ptr, int size){
  assert(ptr && size >= 0);
  unsigned char table1[0x100], table2[0x100], *table, *another;
  assert(ptr && size >= 0);
  memcpy(table1, tcmtftable, sizeof(tcmtftable));
  table = table1;
  another = table2;
  const char *end = ptr + size;
  char *wp = ptr;
  while(ptr < end){
    int idx = *(unsigned char *)ptr;
    unsigned char c = table[idx];
    *(wp++) = c;
    if(idx > 0){
      memcpy(another, &c, 1);
      memcpy(another + 1, table, idx);
      memcpy(another + 1 + idx, table + idx + 1, 255 - idx);
      unsigned char *swap = table;
      table = another;
      another = swap;
    }
    ptr++;
  }
}


/* Encode a region with Elias gamma encoding.
   `ptr' specifies the pointer to the region.
   `size' specifies the size of the region.
   `ptr' specifies the pointer to the output buffer. */
static int tcgammaencode(const char *ptr, int size, char *obuf){
  assert(ptr && size >= 0 && obuf);
  TCBITSTRM strm;
  TCBITSTRMINITW(strm, obuf);
  const char *end = ptr + size;
  while(ptr < end){
    unsigned int c = *(unsigned char *)ptr;
    if(!c){
      TCBITSTRMCAT(strm, 1);
    } else {
      c++;
      int plen = 8;
      while(plen > 0 && !(c & (1 << plen))){
        plen--;
      }
      int jlen = plen;
      while(jlen-- > 0){
        TCBITSTRMCAT(strm, 0);
      }
      while(plen >= 0){
        int sign = (c & (1 << plen)) > 0;
        TCBITSTRMCAT(strm, sign);
        plen--;
      }
    }
    ptr++;
  }
  TCBITSTRMSETEND(strm);
  return TCBITSTRMSIZE(strm);
}


/* Decode a region compressed with Elias gamma encoding.
   `ptr' specifies the pointer to the region.
   `size' specifies the size of the region.
   `ptr' specifies the pointer to the output buffer. */
static int tcgammadecode(const char *ptr, int size, char *obuf){
  assert(ptr && size >= 0 && obuf);
  char *wp = obuf;
  TCBITSTRM strm;
  TCBITSTRMINITR(strm, ptr, size);
  int bnum = TCBITSTRMNUM(strm);
  while(bnum > 0){
    int sign;
    TCBITSTRMREAD(strm, sign);
    bnum--;
    if(sign){
      *(wp++) = 0;
    } else {
      int plen = 1;
      while(bnum > 0){
        TCBITSTRMREAD(strm, sign);
        bnum--;
        if(sign) break;
        plen++;
      }
      unsigned int c = 1;
      while(bnum > 0 && plen-- > 0){
        TCBITSTRMREAD(strm, sign);
        bnum--;
        c = (c << 1) + (sign > 0);
      }
      *(wp++) = c - 1;
    }
  }
  return wp - obuf;;
}



// END OF FILE
