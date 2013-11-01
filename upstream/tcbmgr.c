/*************************************************************************************************
 * The command line utility of the B+ tree database API
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


#include <tcutil.h>
#include <tchdb.h>
#include <tcbdb.h>
#include "myconf.h"

#define RECBUFSIZ      32                // buffer for records


/* global variables */
const char *g_progname;                  // program name
int g_dbgfd;                             // debugging output


/* function prototypes */
int main(int argc, char **argv);
static void usage(void);
static void printerr(TCBDB *bdb);
static int printdata(const char *ptr, int size, bool px);
static char *hextoobj(const char *str, int *sp);
static int runcreate(int argc, char **argv);
static int runinform(int argc, char **argv);
static int runput(int argc, char **argv);
static int runout(int argc, char **argv);
static int runget(int argc, char **argv);
static int runlist(int argc, char **argv);
static int runoptimize(int argc, char **argv);
static int runversion(int argc, char **argv);
static int proccreate(const char *path, int lmemb, int nmemb,
                      int bnum, int apow, int fpow, BDBCMP cmp, int opts);
static int procinform(const char *path, int omode);
static int procput(const char *path, const char *kbuf, int ksiz, const char *vbuf, int vsiz,
                   BDBCMP cmp, int omode, int dmode);
static int procout(const char *path, const char *kbuf, int ksiz, BDBCMP cmp, int omode);
static int procget(const char *path, const char *kbuf, int ksiz, BDBCMP cmp, int omode,
                   bool px, bool pz);
static int proclist(const char *path, BDBCMP cmp, int omode, bool pv, bool bk,
                    const char *jstr, bool px);
static int procoptimize(const char *path, int lmemb, int nmemb,
                        int bnum, int apow, int fpow, BDBCMP cmp, int opts, int omode);
static int procversion(void);


/* main routine */
int main(int argc, char **argv){
  g_progname = argv[0];
  g_dbgfd = -1;
  const char *ebuf = getenv("TCDBGFD");
  if(ebuf) g_dbgfd = atoi(ebuf);
  if(argc < 2) usage();
  int rv = 0;
  if(!strcmp(argv[1], "create")){
    rv = runcreate(argc, argv);
  } else if(!strcmp(argv[1], "inform")){
    rv = runinform(argc, argv);
  } else if(!strcmp(argv[1], "put")){
    rv = runput(argc, argv);
  } else if(!strcmp(argv[1], "out")){
    rv = runout(argc, argv);
  } else if(!strcmp(argv[1], "get")){
    rv = runget(argc, argv);
  } else if(!strcmp(argv[1], "list")){
    rv = runlist(argc, argv);
  } else if(!strcmp(argv[1], "optimize")){
    rv = runoptimize(argc, argv);
  } else if(!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")){
    rv = runversion(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
static void usage(void){
  fprintf(stderr, "%s: the command line utility of the B+ tree database API\n", g_progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s create [-cd|-ci|-cj] [-tl] [-td|-tb] path"
          " [lmemb [nmemb [bnum [apow [fpow]]]]]\n", g_progname);
  fprintf(stderr, "  %s inform [-nl|-nb] path\n", g_progname);
  fprintf(stderr, "  %s put [-cd|-ci|-cj] [-nl|-nb] [-sx] [-dk|-dc|-dd|-db] path"
          " key value\n", g_progname);
  fprintf(stderr, "  %s out [-cd|-ci|-cj] [-nl|-nb] [-sx] path key\n", g_progname);
  fprintf(stderr, "  %s get [-cd|-ci|-cj] [-nl|-nb] [-sx] [-px] [-pz] path key\n", g_progname);
  fprintf(stderr, "  %s list [-cd|-ci|-cj] [-nl|-nb] [-bk] [-pv] [-j str] path\n", g_progname);
  fprintf(stderr, "  %s optimize [-cd|-ci|-cj] [-tl] [-td|-tb] [-tz] [-nl|-nb] path"
          " [lmemb [nmemb [bnum [apow [fpow]]]]]\n", g_progname);
  fprintf(stderr, "  %s version\n", g_progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* print error information */
static void printerr(TCBDB *bdb){
  const char *path = tcbdbpath(bdb);
  int ecode = tcbdbecode(bdb);
  fprintf(stderr, "%s: %s: %d: %s\n", g_progname, path ? path : "-", ecode, tcbdberrmsg(ecode));
}


/* print record data */
static int printdata(const char *ptr, int size, bool px){
  int len = 0;
  while(size-- > 0){
    if(px){
      if(len > 0) putchar(' ');
      len += printf("%02X", *(unsigned char *)ptr);
    } else {
      putchar(*ptr);
      len++;
    }
    ptr++;
  }
  return len;
}


/* create a binary object from a hexadecimal string */
static char *hextoobj(const char *str, int *sp){
  int len = strlen(str);
  char *buf = tcmalloc(len + 1);
  int j = 0;
  for(int i = 0; i < len; i += 2){
    while(strchr(" \n\r\t\f\v", str[i])){
      i++;
    }
    char mbuf[3];
    if((mbuf[0] = str[i]) == '\0') break;
    if((mbuf[1] = str[i+1]) == '\0') break;
    mbuf[2] = '\0';
    buf[j++] = (char)strtol(mbuf, NULL, 16);
  }
  buf[j] = '\0';
  *sp = j;
  return buf;
}


/* parse arguments of create command */
static int runcreate(int argc, char **argv){
  char *path = NULL;
  char *lmstr = NULL;
  char *nmstr = NULL;
  char *bstr = NULL;
  char *astr = NULL;
  char *fstr = NULL;
  BDBCMP cmp = NULL;
  int opts = 0;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-cd")){
        cmp = tcbdbcmpdecimal;
      } else if(!strcmp(argv[i], "-ci")){
        cmp = tcbdbcmpint32;
      } else if(!strcmp(argv[i], "-cj")){
        cmp = tcbdbcmpint64;
      } else if(!strcmp(argv[i], "-tl")){
        opts |= BDBTLARGE;
      } else if(!strcmp(argv[i], "-td")){
        opts |= BDBTDEFLATE;
      } else if(!strcmp(argv[i], "-tb")){
        opts |= BDBTTCBS;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!lmstr){
      lmstr = argv[i];
    } else if(!nmstr){
      nmstr = argv[i];
    } else if(!bstr){
      bstr = argv[i];
    } else if(!astr){
      astr = argv[i];
    } else if(!fstr){
      fstr = argv[i];
    } else {
      usage();
    }
  }
  if(!path) usage();
  int lmemb = lmstr ? atoi(lmstr) : -1;
  int nmemb = nmstr ? atoi(nmstr) : -1;
  int bnum = bstr ? atoi(bstr) : -1;
  int apow = astr ? atoi(astr) : -1;
  int fpow = fstr ? atoi(fstr) : -1;
  int rv = proccreate(path, lmemb, nmemb, bnum, apow, fpow, cmp, opts);
  return rv;
}


/* parse arguments of inform command */
static int runinform(int argc, char **argv){
  char *path = NULL;
  int omode = 0;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-nl")){
        omode |= BDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= BDBOLCKNB;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else {
      usage();
    }
  }
  if(!path) usage();
  int rv = procinform(path, omode);
  return rv;
}


/* parse arguments of put command */
static int runput(int argc, char **argv){
  char *path = NULL;
  char *key = NULL;
  char *value = NULL;
  BDBCMP cmp = NULL;
  int omode = 0;
  int dmode = 0;
  bool sx = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-cd")){
        cmp = tcbdbcmpdecimal;
      } else if(!strcmp(argv[i], "-ci")){
        cmp = tcbdbcmpint32;
      } else if(!strcmp(argv[i], "-cj")){
        cmp = tcbdbcmpint64;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= BDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= BDBOLCKNB;
      } else if(!strcmp(argv[i], "-dk")){
        dmode = -1;
      } else if(!strcmp(argv[i], "-dc")){
        dmode = 1;
      } else if(!strcmp(argv[i], "-dd")){
        dmode = 2;
      } else if(!strcmp(argv[i], "-db")){
        dmode = 3;
      } else if(!strcmp(argv[i], "-sx")){
        sx = true;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!key){
      key = argv[i];
    } else if(!value){
      value = argv[i];
    } else {
      usage();
    }
  }
  if(!path || !key || !value) usage();
  int ksiz, vsiz;
  char *kbuf, *vbuf;
  if(sx){
    kbuf = hextoobj(key, &ksiz);
    vbuf = hextoobj(value, &vsiz);
  } else {
    ksiz = strlen(key);
    kbuf = tcmemdup(key, ksiz);
    vsiz = strlen(value);
    vbuf = tcmemdup(value, vsiz);
  }
  int rv = procput(path, kbuf, ksiz, vbuf, vsiz, cmp, omode, dmode);
  free(vbuf);
  free(kbuf);
  return rv;
}


/* parse arguments of out command */
static int runout(int argc, char **argv){
  char *path = NULL;
  char *key = NULL;
  BDBCMP cmp = NULL;
  int omode = 0;
  bool sx = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-cd")){
        cmp = tcbdbcmpdecimal;
      } else if(!strcmp(argv[i], "-ci")){
        cmp = tcbdbcmpint32;
      } else if(!strcmp(argv[i], "-cj")){
        cmp = tcbdbcmpint64;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= BDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= BDBOLCKNB;
      } else if(!strcmp(argv[i], "-sx")){
        sx = true;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!key){
      key = argv[i];
    } else {
      usage();
    }
  }
  if(!path || !key) usage();
  int ksiz;
  char *kbuf;
  if(sx){
    kbuf = hextoobj(key, &ksiz);
  } else {
    ksiz = strlen(key);
    kbuf = tcmemdup(key, ksiz);
  }
  int rv = procout(path, kbuf, ksiz, cmp, omode);
  free(kbuf);
  return rv;
}


/* parse arguments of get command */
static int runget(int argc, char **argv){
  char *path = NULL;
  char *key = NULL;
  BDBCMP cmp = NULL;
  int omode = 0;
  bool sx = false;
  bool px = false;
  bool pz = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-cd")){
        cmp = tcbdbcmpdecimal;
      } else if(!strcmp(argv[i], "-ci")){
        cmp = tcbdbcmpint32;
      } else if(!strcmp(argv[i], "-cj")){
        cmp = tcbdbcmpint64;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= BDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= BDBOLCKNB;
      } else if(!strcmp(argv[i], "-sx")){
        sx = true;
      } else if(!strcmp(argv[i], "-px")){
        px = true;
      } else if(!strcmp(argv[i], "-pz")){
        pz = true;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!key){
      key = argv[i];
    } else {
      usage();
    }
  }
  if(!path || !key) usage();
  int ksiz;
  char *kbuf;
  if(sx){
    kbuf = hextoobj(key, &ksiz);
  } else {
    ksiz = strlen(key);
    kbuf = tcmemdup(key, ksiz);
  }
  int rv = procget(path, kbuf, ksiz, cmp, omode, px, pz);
  free(kbuf);
  return rv;
}


/* parse arguments of list command */
static int runlist(int argc, char **argv){
  char *path = NULL;
  BDBCMP cmp = NULL;
  int omode = 0;
  bool pv = false;
  bool bk = false;
  char *jstr = NULL;
  bool px = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-cd")){
        cmp = tcbdbcmpdecimal;
      } else if(!strcmp(argv[i], "-ci")){
        cmp = tcbdbcmpint32;
      } else if(!strcmp(argv[i], "-cj")){
        cmp = tcbdbcmpint64;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= BDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= BDBOLCKNB;
      } else if(!strcmp(argv[i], "-pv")){
        pv = true;
      } else if(!strcmp(argv[i], "-bk")){
        bk = true;
      } else if(!strcmp(argv[i], "-j")){
        if(++i >= argc) usage();
        jstr = argv[i];
      } else if(!strcmp(argv[i], "-px")){
        px = true;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else {
      usage();
    }
  }
  if(!path) usage();
  int rv = proclist(path, cmp, omode, pv, bk, jstr, px);
  return rv;
}


/* parse arguments of optimize command */
static int runoptimize(int argc, char **argv){
  char *path = NULL;
  char *lmstr = NULL;
  char *nmstr = NULL;
  char *bstr = NULL;
  char *astr = NULL;
  char *fstr = NULL;
  BDBCMP cmp = NULL;
  int opts = UINT8_MAX;
  int omode = 0;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-cd")){
        cmp = tcbdbcmpdecimal;
      } else if(!strcmp(argv[i], "-ci")){
        cmp = tcbdbcmpint32;
      } else if(!strcmp(argv[i], "-cj")){
        cmp = tcbdbcmpint64;
      } else if(!strcmp(argv[i], "-tl")){
        if(opts == UINT8_MAX) opts = 0;
        opts |= BDBTLARGE;
      } else if(!strcmp(argv[i], "-td")){
        if(opts == UINT8_MAX) opts = 0;
        opts |= BDBTDEFLATE;
      } else if(!strcmp(argv[i], "-tb")){
        if(opts == UINT8_MAX) opts = 0;
        opts |= BDBTTCBS;
      } else if(!strcmp(argv[i], "-tz")){
        if(opts == UINT8_MAX) opts = 0;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= BDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= BDBOLCKNB;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!lmstr){
      lmstr = argv[i];
    } else if(!nmstr){
      nmstr = argv[i];
    } else if(!bstr){
      bstr = argv[i];
    } else if(!astr){
      astr = argv[i];
    } else if(!fstr){
      fstr = argv[i];
    } else {
      usage();
    }
  }
  if(!path) usage();
  int lmemb = lmstr ? atoi(lmstr) : -1;
  int nmemb = nmstr ? atoi(nmstr) : -1;
  int bnum = bstr ? atoi(bstr) : -1;
  int apow = astr ? atoi(astr) : -1;
  int fpow = fstr ? atoi(fstr) : -1;
  int rv = procoptimize(path, lmemb, nmemb, bnum, apow, fpow, cmp, opts, omode);
  return rv;
}


/* parse arguments of version command */
static int runversion(int argc, char **argv){
  int rv = procversion();
  return rv;
}


/* perform create command */
static int proccreate(const char *path, int lmemb, int nmemb,
                      int bnum, int apow, int fpow, BDBCMP cmp, int opts){
  TCBDB *bdb = tcbdbnew();
  if(g_dbgfd >= 0) tcbdbsetdbgfd(bdb, g_dbgfd);
  if(cmp && !tcbdbsetcmpfunc(bdb, cmp, NULL)) printerr(bdb);
  if(!tcbdbtune(bdb, lmemb, nmemb, bnum, apow, fpow, opts)){
    printerr(bdb);
    tcbdbdel(bdb);
    return 1;
  }
  if(!tcbdbopen(bdb, path, BDBOWRITER | BDBOCREAT | BDBOTRUNC)){
    printerr(bdb);
    tcbdbdel(bdb);
    return 1;
  }
  bool err = false;
  if(!tcbdbclose(bdb)){
    printerr(bdb);
    err = true;
  }
  tcbdbdel(bdb);
  return err ? 1 : 0;
}


/* perform inform command */
static int procinform(const char *path, int omode){
  TCBDB *bdb = tcbdbnew();
  if(g_dbgfd >= 0) tcbdbsetdbgfd(bdb, g_dbgfd);
  if(!tcbdbopen(bdb, path, BDBOREADER | omode)){
    printerr(bdb);
    tcbdbdel(bdb);
    return 1;
  }
  bool err = false;
  const char *npath = tcbdbpath(bdb);
  if(!npath) npath = "(unknown)";
  printf("path: %s\n", npath);
  printf("database type: btree\n");
  uint8_t flags = tcbdbflags(bdb);
  printf("additional flags:");
  if(flags & BDBFOPEN) printf(" open");
  if(flags & BDBFFATAL) printf(" fatal");
  printf("\n");
  printf("max leaf member: %d\n", tcbdblmemb(bdb));
  printf("max node member: %d\n", tcbdbnmemb(bdb));
  printf("leaf number: %llu\n", (unsigned long long)tcbdblnum(bdb));
  printf("node number: %llu\n", (unsigned long long)tcbdbnnum(bdb));
  printf("bucket number: %llu\n", (unsigned long long)tcbdbbnum(bdb));
  if(bdb->hdb->cnt_writerec >= 0)
    printf("used bucket number: %lld\n", (long long)tcbdbbnumused(bdb));
  printf("alignment: %u\n", tcbdbalign(bdb));
  printf("free block pool: %u\n", tcbdbfbpmax(bdb));
  uint8_t opts = tcbdbopts(bdb);
  printf("options:");
  if(opts & BDBTLARGE) printf(" large");
  if(opts & BDBTDEFLATE) printf(" deflate");
  if(opts & BDBTTCBS) printf(" tcbs");
  printf("\n");
  printf("record number: %llu\n", (unsigned long long)tcbdbrnum(bdb));
  printf("file size: %llu\n", (unsigned long long)tcbdbfsiz(bdb));
  if(!tcbdbclose(bdb)){
    if(!err) printerr(bdb);
    err = true;
  }
  tcbdbdel(bdb);
  return err ? 1 : 0;
}


/* perform put command */
static int procput(const char *path, const char *kbuf, int ksiz, const char *vbuf, int vsiz,
                   BDBCMP cmp, int omode, int dmode){
  TCBDB *bdb = tcbdbnew();
  if(g_dbgfd >= 0) tcbdbsetdbgfd(bdb, g_dbgfd);
  if(cmp && !tcbdbsetcmpfunc(bdb, cmp, NULL)) printerr(bdb);
  if(!tcbdbopen(bdb, path, BDBOWRITER | omode)){
    printerr(bdb);
    tcbdbdel(bdb);
    return 1;
  }
  bool err = false;
  switch(dmode){
  case -1:
    if(!tcbdbputkeep(bdb, kbuf, ksiz, vbuf, vsiz)){
      printerr(bdb);
      err = true;
    }
    break;
  case 1:
    if(!tcbdbputcat(bdb, kbuf, ksiz, vbuf, vsiz)){
      printerr(bdb);
      err = true;
    }
    break;
  case 2:
    if(!tcbdbputdup(bdb, kbuf, ksiz, vbuf, vsiz)){
      printerr(bdb);
      err = true;
    }
    break;
  case 3:
    if(!tcbdbputdupback(bdb, kbuf, ksiz, vbuf, vsiz)){
      printerr(bdb);
      err = true;
    }
    break;
  default:
    if(!tcbdbput(bdb, kbuf, ksiz, vbuf, vsiz)){
      printerr(bdb);
      err = true;
    }
    break;
  }
  if(!tcbdbclose(bdb)){
    if(!err) printerr(bdb);
    err = true;
  }
  tcbdbdel(bdb);
  return err ? 1 : 0;
}


/* perform out command */
static int procout(const char *path, const char *kbuf, int ksiz, BDBCMP cmp, int omode){
  TCBDB *bdb = tcbdbnew();
  if(g_dbgfd >= 0) tcbdbsetdbgfd(bdb, g_dbgfd);
  if(cmp && !tcbdbsetcmpfunc(bdb, cmp, NULL)) printerr(bdb);
  if(!tcbdbopen(bdb, path, BDBOWRITER | omode)){
    printerr(bdb);
    tcbdbdel(bdb);
    return 1;
  }
  bool err = false;
  if(!tcbdbout(bdb, kbuf, ksiz)){
    printerr(bdb);
    err = true;
  }
  if(!tcbdbclose(bdb)){
    if(!err) printerr(bdb);
    err = true;
  }
  tcbdbdel(bdb);
  return err ? 1 : 0;
}


/* perform get command */
static int procget(const char *path, const char *kbuf, int ksiz, BDBCMP cmp, int omode,
                   bool px, bool pz){
  TCBDB *bdb = tcbdbnew();
  if(g_dbgfd >= 0) tcbdbsetdbgfd(bdb, g_dbgfd);
  if(cmp && !tcbdbsetcmpfunc(bdb, cmp, NULL)) printerr(bdb);
  if(!tcbdbopen(bdb, path, BDBOREADER | omode)){
    printerr(bdb);
    tcbdbdel(bdb);
    return 1;
  }
  bool err = false;
  int vsiz;
  char *vbuf = tcbdbget(bdb, kbuf, ksiz, &vsiz);
  if(vbuf){
    printdata(vbuf, vsiz, px);
    if(!pz) putchar('\n');
    free(vbuf);
  } else {
    printerr(bdb);
    err = true;
  }
  if(!tcbdbclose(bdb)){
    if(!err) printerr(bdb);
    err = true;
  }
  tcbdbdel(bdb);
  return err ? 1 : 0;
}


/* perform list command */
static int proclist(const char *path, BDBCMP cmp, int omode, bool pv, bool bk,
                    const char *jstr, bool px){
  TCBDB *bdb = tcbdbnew();
  if(g_dbgfd >= 0) tcbdbsetdbgfd(bdb, g_dbgfd);
  if(cmp && !tcbdbsetcmpfunc(bdb, cmp, NULL)) printerr(bdb);
  if(!tcbdbopen(bdb, path, BDBOREADER | omode)){
    printerr(bdb);
    tcbdbdel(bdb);
    return 1;
  }
  BDBCUR *cur = tcbdbcurnew(bdb);
  bool err = false;
  if(bk){
    if(jstr){
      if(!tcbdbcurjumpback(cur, jstr, strlen(jstr)) && tcbdbecode(bdb) != TCENOREC){
        printerr(bdb);
        err = true;
      }
    } else {
      if(!tcbdbcurlast(cur) && tcbdbecode(bdb) != TCENOREC){
        printerr(bdb);
        err = true;
      }
    }
  } else {
    if(jstr){
      if(!tcbdbcurjump(cur, jstr, strlen(jstr)) && tcbdbecode(bdb) != TCENOREC){
        printerr(bdb);
        err = true;
      }
    } else {
      if(!tcbdbcurfirst(cur) && tcbdbecode(bdb) != TCENOREC){
        printerr(bdb);
        err = true;
      }
    }
  }
  TCXSTR *key = tcxstrnew();
  TCXSTR *val = tcxstrnew();
  while(tcbdbcurrec(cur, key, val)){
    printdata(tcxstrptr(key), tcxstrsize(key), px);
    if(pv){
      putchar('\t');
      printdata(tcxstrptr(val), tcxstrsize(val), px);
    }
    putchar('\n');
    if(bk){
      if(!tcbdbcurprev(cur) && tcbdbecode(bdb) != TCENOREC){
        printerr(bdb);
        err = true;
      }
    } else {
      if(!tcbdbcurnext(cur) && tcbdbecode(bdb) != TCENOREC){
        printerr(bdb);
        err = true;
      }
    }
  }
  tcxstrdel(val);
  tcxstrdel(key);
  tcbdbcurdel(cur);
  if(!tcbdbclose(bdb)){
    if(!err) printerr(bdb);
    err = true;
  }
  tcbdbdel(bdb);
  return err ? 1 : 0;
}


/* perform optimize command */
static int procoptimize(const char *path, int lmemb, int nmemb,
                        int bnum, int apow, int fpow, BDBCMP cmp, int opts, int omode){
  TCBDB *bdb = tcbdbnew();
  if(g_dbgfd >= 0) tcbdbsetdbgfd(bdb, g_dbgfd);
  if(cmp && !tcbdbsetcmpfunc(bdb, cmp, NULL)) printerr(bdb);
  if(!tcbdbopen(bdb, path, BDBOWRITER | omode)){
    printerr(bdb);
    tcbdbdel(bdb);
    return 1;
  }
  bool err = false;
  if(!tcbdboptimize(bdb, lmemb, nmemb, bnum, apow, fpow, opts)){
    printerr(bdb);
    err = true;
  }
  if(!tcbdbclose(bdb)){
    if(!err) printerr(bdb);
    err = true;
  }
  tcbdbdel(bdb);
  return err ? 1 : 0;
}


/* perform version command */
static int procversion(void){
  printf("Tokyo Cabinet version %s (%d:%s)\n", tcversion, _TC_LIBVER, _TC_FORMATVER);
  printf("Copyright (C) 2006-2007 Mikio Hirabayashi\n");
  return 0;
}



// END OF FILE
