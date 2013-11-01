/*************************************************************************************************
 * The test cases of the fixed-length database API
 *                                                      Copyright (C) 2006-2008 Mikio Hirabayashi
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
#include <tcfdb.h>
#include "myconf.h"

#define RECBUFSIZ      32                // buffer for records
#define EXHEADSIZ      256               // expected header size


/* global variables */
const char *g_progname;                  // program name
int g_dbgfd;                             // debugging output


/* function prototypes */
int main(int argc, char **argv);
static void usage(void);
static void iprintf(const char *format, ...);
static void eprint(TCFDB *fdb, const char *func);
static void mprint(TCFDB *fdb);
static int myrand(int range);
static int runwrite(int argc, char **argv);
static int runread(int argc, char **argv);
static int runremove(int argc, char **argv);
static int runrcat(int argc, char **argv);
static int runmisc(int argc, char **argv);
static int runwicked(int argc, char **argv);
static int procwrite(const char *path, int rnum, int width, int64_t limsiz,
                     bool mt, int omode, bool rnd);
static int procread(const char *path, bool mt, int omode, bool wb, bool rnd);
static int procremove(const char *path, bool mt, int omode, bool rnd);
static int procrcat(const char *path, int rnum, int width, int64_t limsiz,
                    bool mt, int omode, int pnum, bool rl);
static int procmisc(const char *path, int rnum, bool mt, int omode);
static int procwicked(const char *path, int rnum, bool mt, int omode);


/* main routine */
int main(int argc, char **argv){
  g_progname = argv[0];
  g_dbgfd = -1;
  const char *ebuf = getenv("TCDBGFD");
  if(ebuf) g_dbgfd = atoi(ebuf);
  srand((unsigned int)(tctime() * 1000) % UINT_MAX);
  if(argc < 2) usage();
  int rv = 0;
  if(!strcmp(argv[1], "write")){
    rv = runwrite(argc, argv);
  } else if(!strcmp(argv[1], "read")){
    rv = runread(argc, argv);
  } else if(!strcmp(argv[1], "remove")){
    rv = runremove(argc, argv);
  } else if(!strcmp(argv[1], "rcat")){
    rv = runrcat(argc, argv);
  } else if(!strcmp(argv[1], "misc")){
    rv = runmisc(argc, argv);
  } else if(!strcmp(argv[1], "wicked")){
    rv = runwicked(argc, argv);
  } else {
    usage();
  }
  return rv;
}


/* print the usage and exit */
static void usage(void){
  fprintf(stderr, "%s: test cases of the fixed-length database API of Tokyo Cabinet\n",
          g_progname);
  fprintf(stderr, "\n");
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "  %s write [-mt] [-nl|-nb] [-rnd] path rnum [width [limsiz]]\n", g_progname);
  fprintf(stderr, "  %s read [-mt] [-nl|-nb] [-wb] [-rnd] path\n", g_progname);
  fprintf(stderr, "  %s remove [-mt] [-nl|-nb] [-rnd] path\n", g_progname);
  fprintf(stderr, "  %s rcat [-mt] [-nl|-nb] [-pn num] [-rl] path rnum [width [limsiz]]\n",
          g_progname);
  fprintf(stderr, "  %s misc [-mt] [-nl|-nb] path rnum\n", g_progname);
  fprintf(stderr, "  %s wicked [-mt] [-nl|-nb] path rnum\n", g_progname);
  fprintf(stderr, "\n");
  exit(1);
}


/* print formatted information string and flush the buffer */
static void iprintf(const char *format, ...){
  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  fflush(stdout);
  va_end(ap);
}


/* print error message of fixed-length database */
static void eprint(TCFDB *fdb, const char *func){
  const char *path = tcfdbpath(fdb);
  int ecode = tcfdbecode(fdb);
  fprintf(stderr, "%s: %s: %s: error: %d: %s\n",
          g_progname, path ? path : "-", func, ecode, tcfdberrmsg(ecode));
}


/* print members of fixed-length database */
static void mprint(TCFDB *fdb){
  if(fdb->cnt_writerec < 0) return;
  iprintf("minimum ID number: %llu\n", (unsigned long long)tcfdbmin(fdb));
  iprintf("maximum ID number: %llu\n", (unsigned long long)tcfdbmax(fdb));
  iprintf("width of the value: %u\n", (unsigned int)tcfdbwidth(fdb));
  iprintf("limit file size: %llu\n", (unsigned long long)tcfdblimsiz(fdb));
  iprintf("limit ID number: %llu\n", (unsigned long long)tcfdblimid(fdb));
  iprintf("cnt_writerec: %lld\n", (long long)fdb->cnt_writerec);
  iprintf("cnt_readrec: %lld\n", (long long)fdb->cnt_readrec);
  iprintf("cnt_truncfile: %lld\n", (long long)fdb->cnt_truncfile);
}


/* get a random number */
static int myrand(int range){
  return (int)((double)range * rand() / (RAND_MAX + 1.0));
}


/* parse arguments of write command */
static int runwrite(int argc, char **argv){
  char *path = NULL;
  char *rstr = NULL;
  char *wstr = NULL;
  char *lstr = NULL;
  bool mt = false;
  int omode = 0;
  bool rnd = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-mt")){
        mt = true;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= FDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= FDBOLCKNB;
      } else if(!strcmp(argv[i], "-rnd")){
        rnd = true;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!rstr){
      rstr = argv[i];
    } else if(!wstr){
      wstr = argv[i];
    } else if(!lstr){
      lstr = argv[i];
    } else {
      usage();
    }
  }
  if(!path || !rstr) usage();
  int rnum = atoi(rstr);
  if(rnum < 1) usage();
  int width = wstr ? atoi(wstr) : -1;
  int64_t limsiz = lstr ? strtoll(lstr, NULL, 10) : -1;
  int rv = procwrite(path, rnum, width, limsiz, mt, omode, rnd);
  return rv;
}


/* parse arguments of read command */
static int runread(int argc, char **argv){
  char *path = NULL;
  bool mt = false;
  int omode = 0;
  bool wb = false;
  bool rnd = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-mt")){
        mt = true;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= FDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= FDBOLCKNB;
      } else if(!strcmp(argv[i], "-wb")){
        wb = true;
      } else if(!strcmp(argv[i], "-rnd")){
        rnd = true;
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
  int rv = procread(path, mt, omode, wb, rnd);
  return rv;
}


/* parse arguments of remove command */
static int runremove(int argc, char **argv){
  char *path = NULL;
  bool mt = false;
  int omode = 0;
  bool rnd = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-mt")){
        mt = true;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= FDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= FDBOLCKNB;
      } else if(!strcmp(argv[i], "-rnd")){
        rnd = true;
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
  int rv = procremove(path, mt, omode, rnd);
  return rv;
}


/* parse arguments of rcat command */
static int runrcat(int argc, char **argv){
  char *path = NULL;
  char *rstr = NULL;
  char *wstr = NULL;
  char *lstr = NULL;
  bool mt = false;
  int omode = 0;
  int pnum = 0;
  bool rl = false;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-mt")){
        mt = true;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= FDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= FDBOLCKNB;
      } else if(!strcmp(argv[i], "-pn")){
        if(++i >= argc) usage();
        pnum = atoi(argv[i]);
      } else if(!strcmp(argv[i], "-rl")){
        rl = true;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!rstr){
      rstr = argv[i];
    } else if(!wstr){
      wstr = argv[i];
    } else if(!lstr){
      lstr = argv[i];
    } else {
      usage();
    }
  }
  if(!path || !rstr) usage();
  int rnum = atoi(rstr);
  if(rnum < 1) usage();
  int width = wstr ? atoi(wstr) : -1;
  int64_t limsiz = lstr ? strtoll(lstr, NULL, 10) : -1;
  int rv = procrcat(path, rnum, width, limsiz, mt, omode, pnum, rl);
  return rv;
}


/* parse arguments of misc command */
static int runmisc(int argc, char **argv){
  char *path = NULL;
  char *rstr = NULL;
  bool mt = false;
  int omode = 0;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-mt")){
        mt = true;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= FDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= FDBOLCKNB;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!rstr){
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if(!path || !rstr) usage();
  int rnum = atoi(rstr);
  if(rnum < 1) usage();
  int rv = procmisc(path, rnum, mt, omode);
  return rv;
}


/* parse arguments of wicked command */
static int runwicked(int argc, char **argv){
  char *path = NULL;
  char *rstr = NULL;
  bool mt = false;
  int omode = 0;
  for(int i = 2; i < argc; i++){
    if(!path && argv[i][0] == '-'){
      if(!strcmp(argv[i], "-mt")){
        mt = true;
      } else if(!strcmp(argv[i], "-nl")){
        omode |= FDBONOLCK;
      } else if(!strcmp(argv[i], "-nb")){
        omode |= FDBOLCKNB;
      } else {
        usage();
      }
    } else if(!path){
      path = argv[i];
    } else if(!rstr){
      rstr = argv[i];
    } else {
      usage();
    }
  }
  if(!path || !rstr) usage();
  int rnum = atoi(rstr);
  if(rnum < 1) usage();
  int rv = procwicked(path, rnum, mt, omode);
  return rv;
}


/* perform write command */
static int procwrite(const char *path, int rnum, int width, int64_t limsiz,
                     bool mt, int omode, bool rnd){
  iprintf("<Writing Test>\n  path=%s  rnum=%d  width=%d  limsiz=%lld  mt=%d  omode=%d"
          "  rnd=%d\n\n", path, rnum, width, (long long)limsiz, mt, omode, rnd);
  bool err = false;
  double stime = tctime();
  TCFDB *fdb = tcfdbnew();
  if(g_dbgfd >= 0) tcfdbsetdbgfd(fdb, g_dbgfd);
  if(mt && !tcfdbsetmutex(fdb)){
    eprint(fdb, "tcfdbsetmutex");
    err = true;
  }
  if(!tcfdbtune(fdb, width, limsiz)){
    eprint(fdb, "tcfdbtune");
    err = true;
  }
  if(!rnd) omode |= FDBOTRUNC;
  if(!tcfdbopen(fdb, path, FDBOWRITER | FDBOCREAT | omode)){
    eprint(fdb, "tcfdbopen");
    err = true;
  }
  for(int i = 1; i <= rnum; i++){
    char buf[RECBUFSIZ];
    int len = sprintf(buf, "%08d", rnd ? myrand(rnum) + 1 : i);
    if(!tcfdbput2(fdb, buf, len, buf, len)){
      eprint(fdb, "tcfdbput");
      err = true;
      break;
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("record number: %llu\n", (unsigned long long)tcfdbrnum(fdb));
  iprintf("size: %llu\n", (unsigned long long)tcfdbfsiz(fdb));
  mprint(fdb);
  if(!tcfdbclose(fdb)){
    eprint(fdb, "tcfdbclose");
    err = true;
  }
  tcfdbdel(fdb);
  iprintf("time: %.3f\n", tctime() - stime);
  iprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}


/* perform read command */
static int procread(const char *path, bool mt, int omode, bool wb, bool rnd){
  iprintf("<Reading Test>\n  path=%s  mt=%d  omode=%d  wb=%d  rnd=%d\n\n",
          path, mt, omode, wb, rnd);
  bool err = false;
  double stime = tctime();
  TCFDB *fdb = tcfdbnew();
  if(g_dbgfd >= 0) tcfdbsetdbgfd(fdb, g_dbgfd);
  if(mt && !tcfdbsetmutex(fdb)){
    eprint(fdb, "tcfdbsetmutex");
    err = true;
  }
  if(!tcfdbopen(fdb, path, FDBOREADER | omode)){
    eprint(fdb, "tcfdbopen");
    err = true;
  }
  int rnum = tcfdbrnum(fdb);
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%08d", rnd ? myrand(rnum) + 1 : i);
    int vsiz;
    if(wb){
      char vbuf[RECBUFSIZ];
      int vsiz = tcfdbget4(fdb, i, vbuf, RECBUFSIZ);
      if(vsiz < 0 && !(rnd && tcfdbecode(fdb) == TCENOREC)){
        eprint(fdb, "tcfdbget4");
        err = true;
        break;
      }
    } else {
      char *vbuf = tcfdbget2(fdb, kbuf, ksiz, &vsiz);
      if(!vbuf && !(rnd && tcfdbecode(fdb) == TCENOREC)){
        eprint(fdb, "tcfdbget");
        err = true;
        break;
      }
      tcfree(vbuf);
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("record number: %llu\n", (unsigned long long)tcfdbrnum(fdb));
  iprintf("size: %llu\n", (unsigned long long)tcfdbfsiz(fdb));
  mprint(fdb);
  if(!tcfdbclose(fdb)){
    eprint(fdb, "tcfdbclose");
    err = true;
  }
  tcfdbdel(fdb);
  iprintf("time: %.3f\n", tctime() - stime);
  iprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}


/* perform remove command */
static int procremove(const char *path, bool mt, int omode, bool rnd){
  iprintf("<Removing Test>\n  path=%s  mt=%d  omode=%d  rnd=%d\n\n", path, mt, omode, rnd);
  bool err = false;
  double stime = tctime();
  TCFDB *fdb = tcfdbnew();
  if(g_dbgfd >= 0) tcfdbsetdbgfd(fdb, g_dbgfd);
  if(mt && !tcfdbsetmutex(fdb)){
    eprint(fdb, "tcfdbsetmutex");
    err = true;
  }
  if(!tcfdbopen(fdb, path, FDBOWRITER | omode)){
    eprint(fdb, "tcfdbopen");
    err = true;
  }
  int rnum = tcfdbrnum(fdb);
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%08d", rnd ? myrand(rnum) + 1 : i);
    if(!tcfdbout2(fdb, kbuf, ksiz) && !(rnd && tcfdbecode(fdb) == TCENOREC)){
      eprint(fdb, "tcfdbout");
      err = true;
      break;
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("record number: %llu\n", (unsigned long long)tcfdbrnum(fdb));
  iprintf("size: %llu\n", (unsigned long long)tcfdbfsiz(fdb));
  mprint(fdb);
  if(!tcfdbclose(fdb)){
    eprint(fdb, "tcfdbclose");
    err = true;
  }
  tcfdbdel(fdb);
  iprintf("time: %.3f\n", tctime() - stime);
  iprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}


/* perform rcat command */
static int procrcat(const char *path, int rnum, int width, int64_t limsiz,
                    bool mt, int omode, int pnum, bool rl){
  iprintf("<Random Concatenating Test>\n"
          "  path=%s  rnum=%d  width=%d  limsiz=%lld  mt=%d  omode=%d  pnum=%d  rl=%d\n\n",
          path, rnum, width, (long long)limsiz, mt, omode, pnum, rl);
  if(pnum < 1) pnum = rnum;
  bool err = false;
  double stime = tctime();
  TCFDB *fdb = tcfdbnew();
  if(g_dbgfd >= 0) tcfdbsetdbgfd(fdb, g_dbgfd);
  if(mt && !tcfdbsetmutex(fdb)){
    eprint(fdb, "tcfdbsetmutex");
    err = true;
  }
  if(!tcfdbtune(fdb, width, limsiz)){
    eprint(fdb, "tcfdbtune");
    err = true;
  }
  if(!tcfdbopen(fdb, path, FDBOWRITER | FDBOCREAT | FDBOTRUNC | omode)){
    eprint(fdb, "tcfdbopen");
    err = true;
  }
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%d", myrand(pnum) + 1);
    if(rl){
      char vbuf[PATH_MAX];
      int vsiz = myrand(PATH_MAX);
      for(int j = 0; j < vsiz; j++){
        vbuf[j] = myrand(0x100);
      }
      if(!tcfdbputcat2(fdb, kbuf, ksiz, vbuf, vsiz)){
        eprint(fdb, "tcfdbputcat");
        err = true;
        break;
      }
    } else {
      if(!tcfdbputcat2(fdb, kbuf, ksiz, kbuf, ksiz)){
        eprint(fdb, "tcfdbputcat");
        err = true;
        break;
      }
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("record number: %llu\n", (unsigned long long)tcfdbrnum(fdb));
  iprintf("size: %llu\n", (unsigned long long)tcfdbfsiz(fdb));
  mprint(fdb);
  if(!tcfdbclose(fdb)){
    eprint(fdb, "tcfdbclose");
    err = true;
  }
  tcfdbdel(fdb);
  iprintf("time: %.3f\n", tctime() - stime);
  iprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}


/* perform misc command */
static int procmisc(const char *path, int rnum, bool mt, int omode){
  iprintf("<Miscellaneous Test>\n  path=%s  rnum=%d  mt=%d  omode=%d\n\n", path, rnum, mt, omode);
  bool err = false;
  double stime = tctime();
  TCFDB *fdb = tcfdbnew();
  if(g_dbgfd >= 0) tcfdbsetdbgfd(fdb, g_dbgfd);
  if(mt && !tcfdbsetmutex(fdb)){
    eprint(fdb, "tcfdbsetmutex");
    err = true;
  }
  if(!tcfdbtune(fdb, RECBUFSIZ, EXHEADSIZ + (RECBUFSIZ + sizeof(int)) * rnum)){
    eprint(fdb, "tcfdbtune");
    err = true;
  }
  if(!tcfdbopen(fdb, path, FDBOWRITER | FDBOCREAT | FDBOTRUNC | omode)){
    eprint(fdb, "tcfdbopen");
    err = true;
  }
  iprintf("writing:\n");
  for(int i = 1; i <= rnum; i++){
    char buf[RECBUFSIZ];
    int len = sprintf(buf, "%08d", i);
    if(!tcfdbputkeep2(fdb, buf, len, buf, len)){
      eprint(fdb, "tcfdbputkeep");
      err = true;
      break;
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("reading:\n");
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%08d", i);
    int vsiz;
    char *vbuf = tcfdbget2(fdb, kbuf, ksiz, &vsiz);
    if(!vbuf){
      eprint(fdb, "tcfdbget");
      err = true;
      break;
    } else if(vsiz != ksiz || memcmp(vbuf, kbuf, vsiz)){
      eprint(fdb, "(validation)");
      err = true;
      tcfree(vbuf);
      break;
    }
    tcfree(vbuf);
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  if(tcfdbrnum(fdb) != rnum){
    eprint(fdb, "(validation)");
    err = true;
  }
  iprintf("random writing:\n");
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%d", myrand(rnum) + 1);
    char vbuf[RECBUFSIZ];
    int vsiz = myrand(RECBUFSIZ);
    memset(vbuf, '*', vsiz);
    if(!tcfdbput2(fdb, kbuf, ksiz, vbuf, vsiz)){
      eprint(fdb, "tcfdbput");
      err = true;
      break;
    }
    int rsiz;
    char *rbuf = tcfdbget2(fdb, kbuf, ksiz, &rsiz);
    if(!rbuf){
      eprint(fdb, "tcfdbget");
      err = true;
      break;
    }
    if(rsiz != vsiz || memcmp(rbuf, vbuf, rsiz)){
      eprint(fdb, "(validation)");
      err = true;
      tcfree(rbuf);
      break;
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
    tcfree(rbuf);
  }
  iprintf("random erasing:\n");
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%d", myrand(rnum) + 1);
    if(!tcfdbout2(fdb, kbuf, ksiz) && tcfdbecode(fdb) != TCENOREC){
      eprint(fdb, "tcfdbout");
      err = true;
      break;
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("writing:\n");
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "[%d]", i);
    char vbuf[RECBUFSIZ];
    int vsiz = i % RECBUFSIZ;
    memset(vbuf, '*', vsiz);
    if(!tcfdbputkeep2(fdb, kbuf, ksiz, vbuf, vsiz) && tcfdbecode(fdb) != TCEKEEP){
      eprint(fdb, "tcfdbputkeep");
      err = true;
      break;
    }
    if(vsiz < 1){
      char tbuf[PATH_MAX];
      for(int j = 0; j < PATH_MAX; j++){
        tbuf[j] = myrand(0x100);
      }
      if(!tcfdbput2(fdb, kbuf, ksiz, tbuf, PATH_MAX)){
        eprint(fdb, "tcfdbput");
        err = true;
        break;
      }
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("erasing:\n");
  for(int i = 1; i <= rnum; i++){
    if(i % 2 == 1){
      char kbuf[RECBUFSIZ];
      int ksiz = sprintf(kbuf, "[%d]", i);
      if(!tcfdbout2(fdb, kbuf, ksiz)){
        eprint(fdb, "tcfdbout");
        err = true;
        break;
      }
      if(tcfdbout2(fdb, kbuf, ksiz) || tcfdbecode(fdb) != TCENOREC){
        eprint(fdb, "tcfdbout");
        err = true;
        break;
      }
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("random writing and reopening:\n");
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%d", myrand(rnum) + 1);
    int vsiz = myrand(RECBUFSIZ);
    char *vbuf = tcmalloc(vsiz + 1);
    memset(vbuf, '*', vsiz);
    switch(myrand(3)){
    case 0:
      if(!tcfdbput2(fdb, kbuf, ksiz, vbuf, vsiz)){
        eprint(fdb, "tcfdbput");
        err = true;
      }
      break;
    case 1:
      if(!tcfdbputcat2(fdb, kbuf, ksiz, vbuf, vsiz)){
        eprint(fdb, "tcfdbputcat");
        err = true;
      }
      break;
    case 2:
      if(!tcfdbout2(fdb, kbuf, ksiz) && tcfdbecode(fdb) != TCENOREC){
        eprint(fdb, "tcfdbout");
        err = true;
      }
      break;
    }
    tcfree(vbuf);
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  if(!tcfdbclose(fdb)){
    eprint(fdb, "tcfdbclose");
    err = true;
  }
  if(!tcfdbopen(fdb, path, FDBOWRITER | omode)){
    eprint(fdb, "tcfdbopen");
    err = true;
  }
  iprintf("checking:\n");
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "[%d]", i);
    int vsiz;
    char *vbuf = tcfdbget2(fdb, kbuf, ksiz, &vsiz);
    if(vbuf){
      tcfree(vbuf);
    } else if(tcfdbecode(fdb) != TCENOREC){
      eprint(fdb, "tcfdbget");
      err = true;
      break;
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("writing:\n");
  for(int i = 1; i <= rnum; i++){
    char buf[RECBUFSIZ];
    int len = sprintf(buf, "%08d", i);
    if(!tcfdbput2(fdb, buf, len, buf, len)){
      eprint(fdb, "tcfdbput");
      err = true;
      break;
    }
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("reading:\n");
  for(int i = 1; i <= rnum; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%08d", i);
    int vsiz;
    char *vbuf = tcfdbget2(fdb, kbuf, ksiz, &vsiz);
    if(!vbuf){
      eprint(fdb, "tcfdbget");
      err = true;
      break;
    } else if(vsiz != ksiz || memcmp(vbuf, kbuf, vsiz)){
      eprint(fdb, "(validation)");
      err = true;
      tcfree(vbuf);
      break;
    }
    tcfree(vbuf);
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  iprintf("iterator checking:\n");
  if(!tcfdbiterinit(fdb)){
    eprint(fdb, "tcfdbiterinit");
    err = true;
  }
  char *kbuf;
  int ksiz;
  int inum = 0;
  for(int i = 1; (kbuf = tcfdbiternext2(fdb, &ksiz)) != NULL; i++, inum++){
    int vsiz;
    char *vbuf = tcfdbget2(fdb, kbuf, ksiz, &vsiz);
    if(!vbuf){
      eprint(fdb, "tcfdbget2");
      err = true;
      tcfree(kbuf);
      break;
    }
    tcfree(vbuf);
    tcfree(kbuf);
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  if(tcfdbecode(fdb) != TCENOREC || inum != tcfdbrnum(fdb)){
    eprint(fdb, "(validation)");
    err = true;
  }
  iprintf("iteration updating:\n");
  if(!tcfdbiterinit(fdb)){
    eprint(fdb, "tcfdbiterinit");
    err = true;
  }
  inum = 0;
  for(int i = 1; (kbuf = tcfdbiternext2(fdb, &ksiz)) != NULL; i++, inum++){
    if(myrand(2) == 0){
      if(!tcfdbputcat2(fdb, kbuf, ksiz, "0123456789", 10)){
        eprint(fdb, "tcfdbputcat2");
        err = true;
        tcfree(kbuf);
        break;
      }
    } else {
      if(!tcfdbout2(fdb, kbuf, ksiz)){
        eprint(fdb, "tcfdbout");
        err = true;
        tcfree(kbuf);
        break;
      }
    }
    tcfree(kbuf);
    if(rnum > 250 && i % (rnum / 250) == 0){
      putchar('.');
      fflush(stdout);
      if(i == rnum || i % (rnum / 10) == 0) iprintf(" (%08d)\n", i);
    }
  }
  if(tcfdbecode(fdb) != TCENOREC || inum < tcfdbrnum(fdb)){
    eprint(fdb, "(validation)");
    err = true;
  }
  if(!tcfdbsync(fdb)){
    eprint(fdb, "tcfdbsync");
    err = true;
  }
  if(!tcfdbvanish(fdb)){
    eprint(fdb, "tcfdbvanish");
    err = true;
  }
  iprintf("record number: %llu\n", (unsigned long long)tcfdbrnum(fdb));
  iprintf("size: %llu\n", (unsigned long long)tcfdbfsiz(fdb));
  mprint(fdb);
  if(!tcfdbclose(fdb)){
    eprint(fdb, "tcfdbclose");
    err = true;
  }
  tcfdbdel(fdb);
  iprintf("time: %.3f\n", tctime() - stime);
  iprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}


/* perform wicked command */
static int procwicked(const char *path, int rnum, bool mt, int omode){
  iprintf("<Wicked Writing Test>\n  path=%s  rnum=%d  mt=%d  omode=%d\n\n",
          path, rnum, mt, omode);
  bool err = false;
  double stime = tctime();
  TCFDB *fdb = tcfdbnew();
  if(g_dbgfd >= 0) tcfdbsetdbgfd(fdb, g_dbgfd);
  if(mt && !tcfdbsetmutex(fdb)){
    eprint(fdb, "tcfdbsetmutex");
    err = true;
  }
  if(!tcfdbtune(fdb, RECBUFSIZ * 2, EXHEADSIZ + (RECBUFSIZ * 2 + sizeof(int)) * rnum)){
    eprint(fdb, "tcfdbtune");
    err = true;
  }
  if(!tcfdbopen(fdb, path, FDBOWRITER | FDBOCREAT | FDBOTRUNC | omode)){
    eprint(fdb, "tcfdbopen");
    err = true;
  }
  if(!tcfdbiterinit(fdb)){
    eprint(fdb, "tcfdbiterinit");
    err = true;
  }
  TCMAP *map = tcmapnew2(rnum / 5);
  for(int i = 1; i <= rnum && !err; i++){
    uint64_t id = myrand(rnum) + 1;
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%llu", (unsigned long long)id);
    char vbuf[RECBUFSIZ];
    int vsiz = myrand(RECBUFSIZ);
    memset(vbuf, '*', vsiz);
    vbuf[vsiz] = '\0';
    char *rbuf;
    switch(myrand(16)){
    case 0:
      putchar('0');
      if(!tcfdbput2(fdb, kbuf, ksiz, vbuf, vsiz)){
        eprint(fdb, "tcfdbput2");
        err = true;
      }
      tcmapput(map, kbuf, ksiz, vbuf, vsiz);
      break;
    case 1:
      putchar('1');
      if(!tcfdbput3(fdb, kbuf, vbuf)){
        eprint(fdb, "tcfdbput3");
        err = true;
      }
      tcmapput2(map, kbuf, vbuf);
      break;
    case 2:
      putchar('2');
      if(!tcfdbputkeep2(fdb, kbuf, ksiz, vbuf, vsiz) && tcfdbecode(fdb) != TCEKEEP){
        eprint(fdb, "tcfdbputkeep2");
        err = true;
      }
      tcmapputkeep(map, kbuf, ksiz, vbuf, vsiz);
      break;
    case 3:
      putchar('3');
      if(!tcfdbputkeep3(fdb, kbuf, vbuf) && tcfdbecode(fdb) != TCEKEEP){
        eprint(fdb, "tcfdbputkeep3");
        err = true;
      }
      tcmapputkeep2(map, kbuf, vbuf);
      break;
    case 4:
      putchar('4');
      if(!tcfdbputcat2(fdb, kbuf, ksiz, vbuf, vsiz)){
        eprint(fdb, "tcfdbputcat2");
        err = true;
      }
      tcmapputcat(map, kbuf, ksiz, vbuf, vsiz);
      break;
    case 5:
      putchar('5');
      if(!tcfdbputcat3(fdb, kbuf, vbuf)){
        eprint(fdb, "tcfdbputcat3");
        err = true;
      }
      tcmapputcat2(map, kbuf, vbuf);
      break;
    case 6:
      putchar('6');
      if(myrand(10) == 0){
        if(!tcfdbout2(fdb, kbuf, ksiz) && tcfdbecode(fdb) != TCENOREC){
          eprint(fdb, "tcfdbout2");
          err = true;
        }
        tcmapout(map, kbuf, ksiz);
      }
      break;
    case 7:
      putchar('7');
      if(myrand(10) == 0){
        if(!tcfdbout3(fdb, kbuf) && tcfdbecode(fdb) != TCENOREC){
          eprint(fdb, "tcfdbout3");
          err = true;
        }
        tcmapout2(map, kbuf);
      }
      break;
    case 8:
      putchar('8');
      if(!(rbuf = tcfdbget2(fdb, kbuf, ksiz, &vsiz))){
        if(tcfdbecode(fdb) != TCENOREC){
          eprint(fdb, "tcfdbget2");
          err = true;
        }
        rbuf = tcsprintf("[%d]", myrand(i + 1));
        vsiz = strlen(rbuf);
      }
      vsiz += myrand(vsiz);
      rbuf = tcrealloc(rbuf, vsiz + 1);
      for(int j = 0; j < vsiz; j++){
        rbuf[j] = myrand(0x100);
      }
      if(!tcfdbput2(fdb, kbuf, ksiz, rbuf, vsiz)){
        eprint(fdb, "tcfdbput2");
        err = true;
      }
      tcmapput(map, kbuf, ksiz, rbuf, vsiz);
      tcfree(rbuf);
      break;
    case 9:
      putchar('9');
      if(!(rbuf = tcfdbget2(fdb, kbuf, ksiz, &vsiz)) && tcfdbecode(fdb) != TCENOREC){
        eprint(fdb, "tcfdbget2");
        err = true;
      }
      tcfree(rbuf);
      break;
    case 10:
      putchar('A');
      if(!(rbuf = tcfdbget3(fdb, kbuf)) && tcfdbecode(fdb) != TCENOREC){
        eprint(fdb, "tcfdbget3");
        err = true;
      }
      tcfree(rbuf);
      break;
    case 11:
      putchar('B');
      if(myrand(1) == 0) vsiz = 1;
      if((vsiz = tcfdbget4(fdb, id, vbuf, vsiz)) < 0 && tcfdbecode(fdb) != TCENOREC){
        eprint(fdb, "tcfdbget4");
        err = true;
      }
      break;
    case 12:
      putchar('C');
      if(myrand(rnum / 50) == 0){
        if(!tcfdbiterinit(fdb)){
          eprint(fdb, "tcfdbiterinit");
          err = true;
        }
      }
      for(int j = myrand(rnum) / 1000 + 1; j >= 0; j--){
        if(tcfdbiternext(fdb) < 0){
          int ecode = tcfdbecode(fdb);
          if(ecode != TCEINVALID && ecode != TCENOREC){
            eprint(fdb, "tcfdbiternext");
            err = true;
          }
        }
      }
      break;
    default:
      putchar('@');
      if(myrand(10000) == 0) srand((unsigned int)(tctime() * 1000) % UINT_MAX);
      if(myrand(rnum / 16 + 1) == 0){
        int cnt = myrand(30);
        for(int j = 0; j < rnum && !err; j++){
          ksiz = sprintf(kbuf, "%d", i + j);
          if(tcfdbout2(fdb, kbuf, ksiz)){
            cnt--;
          } else if(tcfdbecode(fdb) != TCENOREC){
            eprint(fdb, "tcbdbout2");
            err = true;
          }
          tcmapout(map, kbuf, ksiz);
          if(cnt < 0) break;
        }
      }
      break;
    }
    if(i % 50 == 0) iprintf(" (%08d)\n", i);
    if(i == rnum / 2){
      if(!tcfdbclose(fdb)){
        eprint(fdb, "tcfdbclose");
        err = true;
      }
      if(!tcfdbopen(fdb, path, FDBOWRITER | omode)){
        eprint(fdb, "tcfdbopen");
        err = true;
      }
    } else if(i == rnum / 4){
      char *npath = tcsprintf("%s-tmp", path);
      if(!tcfdbcopy(fdb, npath)){
        eprint(fdb, "tcfdbcopy");
        err = true;
      }
      TCFDB *nfdb = tcfdbnew();
      if(!tcfdbopen(nfdb, npath, FDBOREADER | omode)){
        eprint(nfdb, "tcfdbopen");
        err = true;
      }
      tcfdbdel(nfdb);
      unlink(npath);
      tcfree(npath);
      if(!tcfdboptimize(fdb, RECBUFSIZ, -1)){
        eprint(fdb, "tcfdboptimize");
        err = true;
      }
      if(!tcfdbiterinit(fdb)){
        eprint(fdb, "tcfdbiterinit");
        err = true;
      }
    }
  }
  if(rnum % 50 > 0) iprintf(" (%08d)\n", rnum);
  if(!tcfdbsync(fdb)){
    eprint(fdb, "tcfdbsync");
    err = true;
  }
  if(tcfdbrnum(fdb) != tcmaprnum(map)){
    eprint(fdb, "(validation)");
    err = true;
  }
  for(int i = 1; i <= rnum && !err; i++){
    char kbuf[RECBUFSIZ];
    int ksiz = sprintf(kbuf, "%d", i);
    int vsiz;
    const char *vbuf = tcmapget(map, kbuf, ksiz, &vsiz);
    int rsiz;
    char *rbuf = tcfdbget2(fdb, kbuf, ksiz, &rsiz);
    if(vbuf){
      putchar('.');
      if(vsiz > RECBUFSIZ) vsiz = RECBUFSIZ;
      if(!rbuf){
        eprint(fdb, "tcfdbget2");
        err = true;
      } else if(rsiz != vsiz || memcmp(rbuf, vbuf, rsiz)){
        eprint(fdb, "(validation)");
        err = true;
      }
    } else {
      putchar('*');
      if(rbuf || tcfdbecode(fdb) != TCENOREC){
        eprint(fdb, "(validation)");
        err = true;
      }
    }
    tcfree(rbuf);
    if(i % 50 == 0) iprintf(" (%08d)\n", i);
  }
  if(rnum % 50 > 0) iprintf(" (%08d)\n", rnum);
  tcmapiterinit(map);
  int ksiz;
  const char *kbuf;
  for(int i = 1; (kbuf = tcmapiternext(map, &ksiz)) != NULL; i++){
    putchar('+');
    int vsiz;
    const char *vbuf = tcmapiterval(kbuf, &vsiz);
    if(vsiz > RECBUFSIZ) vsiz = RECBUFSIZ;
    int rsiz;
    char *rbuf = tcfdbget2(fdb, kbuf, ksiz, &rsiz);
    if(!rbuf){
      eprint(fdb, "tcfdbget2");
      err = true;
    } else if(rsiz != vsiz || memcmp(rbuf, vbuf, rsiz)){
      eprint(fdb, "(validation)");
      err = true;
    }
    tcfree(rbuf);
    if(!tcfdbout2(fdb, kbuf, ksiz)){
      eprint(fdb, "tcfdbout2");
      err = true;
    }
    if(i % 50 == 0) iprintf(" (%08d)\n", i);
  }
  int mrnum = tcmaprnum(map);
  if(mrnum % 50 > 0) iprintf(" (%08d)\n", mrnum);
  if(tcfdbrnum(fdb) != 0){
    eprint(fdb, "(validation)");
    err = true;
  }
  iprintf("record number: %llu\n", (unsigned long long)tcfdbrnum(fdb));
  iprintf("size: %llu\n", (unsigned long long)tcfdbfsiz(fdb));
  mprint(fdb);
  tcmapdel(map);
  if(!tcfdbclose(fdb)){
    eprint(fdb, "tcfdbclose");
    err = true;
  }
  tcfdbdel(fdb);
  iprintf("time: %.3f\n", tctime() - stime);
  iprintf("%s\n\n", err ? "error" : "ok");
  return err ? 1 : 0;
}



// END OF FILE
