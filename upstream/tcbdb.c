/*************************************************************************************************
 * The B+ tree database API of Tokyo Cabinet
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


#include "tcbdb.h"
#include "tchdb.h"
#include "tcutil.h"
#include "myconf.h"

#define BDBFILEMODE    00644             // permission of created files
#define BDBOPAQUESIZ   64                // size of using opaque field
#define BDBPAGEBUFSIZ  32768             // size of a buffer to read each page
#define BDBNODEIDBASE   ((1LL<<48)+1)    // base number of node ID
#define BDBLEVELMAX    64                // max level of B+ tree
#define BDBCACHEOUT    8                 // number of pages in a process of cacheout

#define BDBDEFLMEMB    128               // default number of members in each leaf
#define BDBMINLMEMB    4                 // minimum number of members in each leaf
#define BDBDEFNMEMB    256               // default number of members in each node
#define BDBMINNMEMB    4                 // minimum number of members in each node
#define BDBDEFBNUM     16381             // default bucket number
#define BDBDEFAPOW     8                 // default alignment power
#define BDBDEFFPOW     10                // default free block pool power
#define BDBDEFLCNUM    1024              // default number of leaf cache
#define BDBDEFNCNUM    512               // default number of node cache

typedef struct {                         // type of structure for a record
  char *kbuf;                            // pointer to the key region
  int ksiz;                              // size of the key region
  char *vbuf;                            // pointer to the value region
  int vsiz;                              // size of the value region
  TCLIST *rest;                          // list of value objects
} BDBREC;

typedef struct {                         // type of structure for a leaf page
  uint64_t id;                           // ID number of the leaf
  TCLIST *recs;                          // list of records
  uint64_t prev;                         // ID number of the previous leaf
  uint64_t next;                         // ID number of the next leaf
  bool dirty;                            // whether to be written back
} BDBLEAF;

typedef struct {                         // type of structure for a page index
  uint64_t pid;                          // ID number of the referring page
  char *kbuf;                            // pointer to the key region
  int ksiz;                              // size of the key region
} BDBIDX;

typedef struct {                         // type of structure for a node page
  uint64_t id;                           // ID number of the node
  uint64_t heir;                         // ID of the child before the first index
  TCLIST *idxs;                          // list of indexes
  bool dirty;                            // whether to be written back
} BDBNODE;

enum {                                   // enumeration for duplication behavior
  BDBPDOVER,                             // overwrite an existing value
  BDBPDKEEP,                             // keep the existing value
  BDBPDCAT,                              // concatenate values
  BDBPDDUP,                              // allow duplication of keys
  BDBPDDUPB,                             // allow backward duplication
};


/* private function prototypes */
static void tcbdbclear(TCBDB *bdb);
static void tcdumpmeta(TCBDB *bdb);
static void tcloadmeta(TCBDB *bdb);
static BDBLEAF *tcbdbleafnew(TCBDB *bdb, uint64_t prev, uint64_t next);
static bool tcbdbleafcacheout(TCBDB *bdb, uint64_t id);
static bool tcbdbleafsave(TCBDB *bdb, BDBLEAF *leaf);
static BDBLEAF *tcbdbleafload(TCBDB *bdb, uint64_t id);
static BDBLEAF *tcbdbgethistleaf(TCBDB *bdb, const char *kbuf, int ksiz);
static bool tcbdbleafaddrec(TCBDB *bdb, BDBLEAF *leaf, int dmode,
                            const char *kbuf, int ksiz, const char *vbuf, int vsiz);
static int tcbdbleafdatasize(BDBLEAF *leaf);
static BDBLEAF *tcbdbleafdivide(TCBDB *bdb, BDBLEAF *leaf);
static BDBNODE *tcbdbnodenew(TCBDB *bdb, uint64_t heir);
static bool tcbdbnodecacheout(TCBDB *bdb, uint64_t id);
static bool tcbdbnodesave(TCBDB *bdb, BDBNODE *node);
static BDBNODE *tcbdbnodeload(TCBDB *bdb, uint64_t id);
static void tcbdbnodeaddidx(TCBDB *bdb, BDBNODE *node, bool order, uint64_t pid,
                            const char *kbuf, int ksiz);
static uint64_t tcbdbsearchleaf(TCBDB *bdb, const char *kbuf, int ksiz);
static BDBREC *tcbdbsearchrec(TCBDB *bdb, BDBLEAF *leaf, const char *kbuf, int ksiz, int *ip);
static bool tcbdbcacheadjust(TCBDB *bdb);
static void tcbdbcachepurge(TCBDB *bdb);
static bool tcbdbopenimpl(TCBDB *bdb, const char *path, int omode);
static bool tcbdbcloseimpl(TCBDB *bdb);
static bool tcbdbputimpl(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz,
                         int dmode);
static bool tcbdboutimpl(TCBDB *bdb, const char *kbuf, int ksiz);
static bool tcbdboutlist(TCBDB *bdb, const char *kbuf, int ksiz);
static const char *tcbdbgetimpl(TCBDB *bdb, const char *kbuf, int ksiz, int *sp);
static int tcbdbgetnum(TCBDB *bdb, const char *kbuf, int ksiz);
static TCLIST *tcbdbgetlist(TCBDB *bdb, const char *kbuf, int ksiz);
static bool tcbdboptimizeimpl(TCBDB *bdb, int32_t lmemb, int32_t nmemb,
                              int64_t bnum, int8_t apow, int8_t fpow, uint8_t opts);
static bool tcbdblockmethod(TCBDB *bdb, bool wr);
static bool tcbdbunlockmethod(TCBDB *bdb);
static bool tcbdblockcache(TCBDB *bdb);
static bool tcbdbunlockcache(TCBDB *bdb);
static bool tcbdblocktran(TCBDB *bdb);
static bool tcbdbunlocktran(TCBDB *bdb);
static bool tcbdbcurfirstimpl(BDBCUR *cur);
static bool tcbdbcurlastimpl(BDBCUR *cur);
static bool tcbdbcurjumpimpl(BDBCUR *cur, const char *kbuf, int ksiz, bool forward);
static bool tcbdbcuradjust(BDBCUR *cur, bool forward);
static bool tcbdbcurprevimpl(BDBCUR *cur);
static bool tcbdbcurnextimpl(BDBCUR *cur);
static bool tcbdbcurputimpl(BDBCUR *cur, const char *vbuf, int vsiz, int mode);
static bool tcbdbcuroutimpl(BDBCUR *cur);
static bool tcbdbcurrecimpl(BDBCUR *cur, const char **kbp, int *ksp, const char **vbp, int *vsp);


/* debugging function prototypes */
void tcbdbprintmeta(TCBDB *bdb);
void tcbdbprintleaf(TCBDB *bdb, BDBLEAF *leaf);
void tcbdbprintnode(TCBDB *bdb, BDBNODE *node);



/*************************************************************************************************
 * API
 *************************************************************************************************/


/* Get the message string corresponding to an error code. */
const char *tcbdberrmsg(int ecode){
  return tchdberrmsg(ecode);
}


/* Create a B+ tree database object. */
TCBDB *tcbdbnew(void){
  TCBDB *bdb = tcmalloc(sizeof(*bdb));
  tcbdbclear(bdb);
  bdb->hdb = tchdbnew();
  bdb->hist = tcmalloc(sizeof(*bdb->hist) * BDBLEVELMAX);
  return bdb;
}


/* Delete a B+ tree database object. */
void tcbdbdel(TCBDB *bdb){
  assert(bdb);
  free(bdb->hist);
  tchdbdel(bdb->hdb);
  if(bdb->mmtx){
    pthread_mutex_destroy(bdb->tmtx);
    pthread_mutex_destroy(bdb->cmtx);
    pthread_rwlock_destroy(bdb->mmtx);
    free(bdb->tmtx);
    free(bdb->cmtx);
    free(bdb->mmtx);
  }
  free(bdb);
}


/* Get the last happened error code of a B+ tree database object. */
int tcbdbecode(TCBDB *bdb){
  assert(bdb);
  return tchdbecode(bdb->hdb);
}


/* Set mutual exclusion control of a B+ tree database object for threading. */
bool tcbdbsetmutex(TCBDB *bdb){
  assert(bdb);
  if(!TCUSEPTHREAD) return true;
  if(!tcglobalmutexlock()) return false;
  if(bdb->mmtx || bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcglobalmutexunlock();
    return false;
  }
  bdb->mmtx = tcmalloc(sizeof(pthread_rwlock_t));
  bdb->cmtx = tcmalloc(sizeof(pthread_mutex_t));
  bdb->tmtx = tcmalloc(sizeof(pthread_mutex_t));
  if(pthread_rwlock_init(bdb->mmtx, NULL) != 0 || pthread_mutex_init(bdb->cmtx, NULL) != 0 ||
     pthread_mutex_init(bdb->tmtx, NULL) != 0){
    free(bdb->tmtx);
    free(bdb->cmtx);
    free(bdb->mmtx);
    bdb->tmtx = NULL;
    bdb->cmtx = NULL;
    bdb->mmtx = NULL;
    tcglobalmutexunlock();
    return false;
  }
  tcglobalmutexunlock();
  return tchdbsetmutex(bdb->hdb);
}


/* Set the custom comparison function of a B+ tree database object. */
bool tcbdbsetcmpfunc(TCBDB *bdb, BDBCMP cmp, void *cmpop){
  assert(bdb && cmp);
  if(bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return false;
  }
  bdb->cmp = cmp;
  bdb->cmpop = cmpop;
  return true;
}


/* Set the tuning parameters of a B+ tree database object. */
bool tcbdbtune(TCBDB *bdb, int32_t lmemb, int32_t nmemb,
               int64_t bnum, int8_t apow, int8_t fpow, uint8_t opts){
  assert(bdb);
  if(bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return false;
  }
  bdb->lmemb = (lmemb > 0) ? tclmax(lmemb, BDBMINLMEMB) : BDBDEFLMEMB;
  bdb->nmemb = (nmemb > 0) ? tclmax(nmemb, BDBMINNMEMB) : BDBDEFNMEMB;
  bdb->opts = opts;
  uint8_t hopts = 0;
  if(opts & BDBTLARGE) hopts |= HDBTLARGE;
  if(opts & BDBTDEFLATE) hopts |= HDBTDEFLATE;
  if(opts & BDBTTCBS) hopts |= HDBTTCBS;
  bnum = (bnum > 0) ? bnum : BDBDEFBNUM;
  apow = (apow >= 0) ? apow : BDBDEFAPOW;
  fpow = (fpow >= 0) ? fpow : BDBDEFFPOW;
  return tchdbtune(bdb->hdb, bnum, apow, fpow, hopts);
}


/* Set the caching parameters of a B+ tree database object. */
bool tcbdbsetcache(TCBDB *bdb, int32_t lcnum, int32_t ncnum){
  assert(bdb);
  if(bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return false;
  }
  if(lcnum > 0) bdb->lcnum = tclmax(lcnum, BDBLEVELMAX);
  if(ncnum > 0) bdb->ncnum = tclmax(ncnum, BDBLEVELMAX);
  return true;
}


/* Open a database file and connect a B+ tree database object. */
bool tcbdbopen(TCBDB *bdb, const char *path, int omode){
  assert(bdb && path);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbopenimpl(bdb, path, omode);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Close a B+ tree database object. */
bool tcbdbclose(TCBDB *bdb){
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcloseimpl(bdb);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Store a record into a B+ tree database object. */
bool tcbdbput(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz){
  assert(bdb && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbputimpl(bdb, kbuf, ksiz, vbuf, vsiz, BDBPDOVER);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Store a string record into a B+ tree database object. */
bool tcbdbput2(TCBDB *bdb, const char *kstr, const char *vstr){
  assert(bdb && kstr && vstr);
  return tcbdbput(bdb, kstr, strlen(kstr), vstr, strlen(vstr));
}


/* Store a new record into a B+ tree database object. */
bool tcbdbputkeep(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz){
  assert(bdb && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbputimpl(bdb, kbuf, ksiz, vbuf, vsiz, BDBPDKEEP);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Store a new string record into a B+ tree database object. */
bool tcbdbputkeep2(TCBDB *bdb, const char *kstr, const char *vstr){
  assert(bdb && kstr && vstr);
  return tcbdbputkeep(bdb, kstr, strlen(kstr), vstr, strlen(vstr));
}


/* Concatenate a value at the end of the existing record in a B+ tree database object. */
bool tcbdbputcat(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz){
  assert(bdb && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbputimpl(bdb, kbuf, ksiz, vbuf, vsiz, BDBPDCAT);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Concatenate a stirng value at the end of the existing record in a B+ tree database object. */
bool tcbdbputcat2(TCBDB *bdb, const char *kstr, const char *vstr){
  assert(bdb && kstr && vstr);
  return tcbdbputcat(bdb, kstr, strlen(kstr), vstr, strlen(vstr));
}


/* Store a new record into a B+ tree database object with allowing duplication of keys. */
bool tcbdbputdup(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz){
  assert(bdb && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbputimpl(bdb, kbuf, ksiz, vbuf, vsiz, BDBPDDUP);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Store a new string record into a B+ tree database object with allowing duplication of keys. */
bool tcbdbputdup2(TCBDB *bdb, const char *kstr, const char *vstr){
  assert(bdb && kstr && vstr);
  return tcbdbputdup(bdb, kstr, strlen(kstr), vstr, strlen(vstr));
}


/* Remove a record of a B+ tree database object. */
bool tcbdbout(TCBDB *bdb, const void *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdboutimpl(bdb, kbuf, ksiz);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Remove a string record of a B+ tree database object. */
bool tcbdbout2(TCBDB *bdb, const char *kstr){
  assert(bdb && kstr);
  return tcbdbout(bdb, kstr, strlen(kstr));
}


/* Remove records of a B+ tree database object. */
bool tcbdbout3(TCBDB *bdb, const void *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdboutlist(bdb, kbuf, ksiz);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Retrieve a record in a B+ tree database object. */
void *tcbdbget(TCBDB *bdb, const void *kbuf, int ksiz, int *sp){
  assert(bdb && kbuf && ksiz >= 0 && sp);
  if(!tcbdblockmethod(bdb, false)) return NULL;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return NULL;
  }
  const char *vbuf = tcbdbgetimpl(bdb, kbuf, ksiz, sp);
  char *rv = vbuf ? tcmemdup(vbuf, *sp) : NULL;
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Retrieve a string record in a B+ tree database object. */
char *tcbdbget2(TCBDB *bdb, const char *kstr){
  assert(bdb && kstr);
  int vsiz;
  return tcbdbget(bdb, kstr, strlen(kstr), &vsiz);
}


/* Retrieve a record in a B+ tree database object and write the value into a buffer. */
const void *tcbdbget3(TCBDB *bdb, const void *kbuf, int ksiz, int *sp){
  assert(bdb && kbuf && ksiz >= 0 && sp);
  if(!tcbdblockmethod(bdb, false)) return NULL;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return NULL;
  }
  const char *rv = tcbdbgetimpl(bdb, kbuf, ksiz, sp);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Retrieve records in a B+ tree database object. */
TCLIST *tcbdbget4(TCBDB *bdb, const void *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  if(!tcbdblockmethod(bdb, false)) return NULL;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return NULL;
  }
  TCLIST *rv = tcbdbgetlist(bdb, kbuf, ksiz);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the number of records corresponding a key in a B+ tree database object. */
int tcbdbvnum(TCBDB *bdb, const void *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  if(!tcbdblockmethod(bdb, false)) return 0;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return 0;
  }
  int rv = tcbdbgetnum(bdb, kbuf, ksiz);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the size of the value of a record in a B+ tree database object. */
int tcbdbvsiz(TCBDB *bdb, const void *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  int vsiz;
  if(!tcbdbget3(bdb, kbuf, ksiz, &vsiz)) return -1;
  return vsiz;
}


/* Synchronize updated contents of a B+ tree database object with the file and the device. */
bool tcbdbsync(TCBDB *bdb){
  assert(bdb);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode || bdb->tran){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbmemsync(bdb, true);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Optimize the file of a B+ tree database object. */
bool tcbdboptimize(TCBDB *bdb, int32_t lmemb, int32_t nmemb,
                   int64_t bnum, int8_t apow, int8_t fpow, uint8_t opts){
  assert(bdb);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode || bdb->tran){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdboptimizeimpl(bdb, lmemb, nmemb, bnum, apow, fpow, opts);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Begin the transaction of a B+ tree database object. */
bool tcbdbtranbegin(TCBDB *bdb){
  assert(bdb);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode || bdb->tran){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(!tcbdbmemsync(bdb, false)){
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(!tcbdblocktran(bdb)){
    tcbdbunlockmethod(bdb);
    return false;
  }
  bdb->tran = true;
  bdb->rbopaque = tcmemdup(bdb->opaque, BDBOPAQUESIZ);
  tcbdbunlockmethod(bdb);
  return true;
}


/* Commit the transaction of a B+ tree database object. */
bool tcbdbtrancommit(TCBDB *bdb){
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode || !bdb->tran){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  free(bdb->rbopaque);
  bdb->tran = false;
  bdb->rbopaque = NULL;
  bool err = false;
  if(!tcbdbmemsync(bdb, false)) err = true;
  tcbdbunlocktran(bdb);
  tcbdbunlockmethod(bdb);
  return err ? false : true;
}


/* Abort the transaction of a B+ tree database object. */
bool tcbdbtranabort(TCBDB *bdb){
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode || !bdb->tran){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  tcbdbcachepurge(bdb);
  memcpy(bdb->opaque, bdb->rbopaque, BDBOPAQUESIZ);
  tcloadmeta(bdb);
  free(bdb->rbopaque);
  bdb->tran = false;
  bdb->rbopaque = NULL;
  tcbdbunlocktran(bdb);
  tcbdbunlockmethod(bdb);
  return true;
}


/* Get the file path of a B+ tree database object. */
const char *tcbdbpath(TCBDB *bdb){
  assert(bdb);
  if(!tcbdblockmethod(bdb, false)) return NULL;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return NULL;
  }
  const char *rv = tchdbpath(bdb->hdb);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the number of records of a B+ tree database object. */
uint64_t tcbdbrnum(TCBDB *bdb){
  assert(bdb);
  if(!tcbdblockmethod(bdb, false)) return 0;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return 0;
  }
  uint64_t rv = bdb->rnum;
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the size of the database file of a B+ tree database object. */
uint64_t tcbdbfsiz(TCBDB *bdb){
  assert(bdb);
  if(!tcbdblockmethod(bdb, false)) return 0;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return 0;
  }
  uint64_t rv = tchdbfsiz(bdb->hdb);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Create a cursor object. */
BDBCUR *tcbdbcurnew(TCBDB *bdb){
  assert(bdb);
  BDBCUR *cur = tcmalloc(sizeof(*cur));
  cur->bdb = bdb;
  cur->id = 0;
  cur->kidx = 0;
  cur->vidx = 0;
  return cur;
}


/* Delete a cursor object. */
void tcbdbcurdel(BDBCUR *cur){
  assert(cur);
  free(cur);
}


/* Move a cursor object to the first record. */
bool tcbdbcurfirst(BDBCUR *cur){
  assert(cur);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcurfirstimpl(cur);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Move a cursor object to the last record. */
bool tcbdbcurlast(BDBCUR *cur){
  assert(cur);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcurlastimpl(cur);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Move a cursor object to the front of records corresponding a key. */
bool tcbdbcurjump(BDBCUR *cur, const char *kbuf, int ksiz){
  assert(cur && kbuf && ksiz >= 0);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcurjumpimpl(cur, kbuf, ksiz, true);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Move a cursor object to the previous record. */
bool tcbdbcurprev(BDBCUR *cur){
  assert(cur);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcurprevimpl(cur);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Move a cursor object to the next record. */
bool tcbdbcurnext(BDBCUR *cur){
  assert(cur);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcurnextimpl(cur);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Insert a record around a cursor object. */
bool tcbdbcurput(BDBCUR *cur, const char *vbuf, int vsiz, int cpmode){
  assert(cur && vbuf && vsiz >= 0);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcurputimpl(cur, vbuf, vsiz, cpmode);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Insert a string record around a cursor object. */
bool tcbdbcurput2(BDBCUR *cur, const char *vstr, int cpmode){
  assert(cur && vstr);
  return tcbdbcurput(cur, vstr, strlen(vstr), cpmode);
}


/* Delete the record where a cursor object is. */
bool tcbdbcurout(BDBCUR *cur){
  assert(cur);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcuroutimpl(cur);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the key of the record where the cursor object is. */
char *tcbdbcurkey(BDBCUR *cur, int *sp){
  assert(cur && sp);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  char *rv;
  if(tcbdbcurrecimpl(cur, &kbuf, &ksiz, &vbuf, &vsiz)){
    rv = tcmemdup(kbuf, ksiz);
    *sp = ksiz;
  } else {
    rv = NULL;
  }
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the key string of the record where the cursor object is. */
char *tcbdbcurkey2(BDBCUR *cur){
  assert(cur);
  int ksiz;
  return tcbdbcurkey(cur, &ksiz);
}


/* Get the key of the record where the cursor object is, as a volatile buffer. */
const char *tcbdbcurkey3(BDBCUR *cur, int *sp){
  assert(cur && sp);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  const char *rv;
  if(tcbdbcurrecimpl(cur, &kbuf, &ksiz, &vbuf, &vsiz)){
    rv = kbuf;
    *sp = ksiz;
  } else {
    rv = NULL;
  }
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the value of the record where the cursor object is. */
char *tcbdbcurval(BDBCUR *cur, int *sp){
  assert(cur && sp);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  char *rv;
  if(tcbdbcurrecimpl(cur, &kbuf, &ksiz, &vbuf, &vsiz)){
    rv = tcmemdup(vbuf, vsiz);
    *sp = vsiz;
  } else {
    rv = NULL;
  }
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the value string of the record where the cursor object is. */
char *tcbdbcurval2(BDBCUR *cur){
  assert(cur);
  int vsiz;
  return tcbdbcurval(cur, &vsiz);
}


/* Get the value of the record where the cursor object is, as a volatile buffer. */
const char *tcbdbcurval3(BDBCUR *cur, int *sp){
  assert(cur && sp);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  const char *rv;
  if(tcbdbcurrecimpl(cur, &kbuf, &ksiz, &vbuf, &vsiz)){
    rv = vbuf;
    *sp = vsiz;
  } else {
    rv = NULL;
  }
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Get the key and the value of the record where the cursor object is. */
bool tcbdbcurrec(BDBCUR *cur, TCXSTR *kxstr, TCXSTR *vxstr){
  assert(cur && kxstr && vxstr);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  if(cur->id < 1){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  bool rv;
  if(tcbdbcurrecimpl(cur, &kbuf, &ksiz, &vbuf, &vsiz)){
    tcxstrclear(kxstr);
    tcxstrcat(kxstr, kbuf, ksiz);
    tcxstrclear(vxstr);
    tcxstrcat(vxstr, vbuf, vsiz);
    rv = true;
  } else {
    rv = false;
  }
  tcbdbunlockmethod(bdb);
  return rv;
}



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Set the file descriptor for debugging output.
   `bdb' specifies the B+ tree database object.
   `fd' specifies the file descriptor for debugging output. */
void tcbdbsetdbgfd(TCBDB *bdb, int fd){
  assert(bdb && fd >= 0);
  tchdbsetdbgfd(bdb->hdb, fd);
}


/* Get the file descriptor for debugging output. */
int tcbdbdbgfd(TCBDB *bdb){
  assert(bdb);
  return tchdbdbgfd(bdb->hdb);
}


/* Synchronize updating contents on memory. */
bool tcbdbmemsync(TCBDB *bdb, bool phys){
  assert(bdb);
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return false;
  }
  bool err = false;
  bool clk = tcbdblockcache(bdb);
  const char *vbuf;
  int vsiz;
  TCMAP *leafc = bdb->leafc;
  tcmapiterinit(leafc);
  while((vbuf = tcmapiternext(leafc, &vsiz)) != NULL){
    int rsiz;
    BDBLEAF *leaf = (BDBLEAF *)tcmapget(bdb->leafc, vbuf, vsiz, &rsiz);
    if(leaf->dirty && !tcbdbleafsave(bdb, leaf)) err = true;
  }
  TCMAP *nodec = bdb->nodec;
  tcmapiterinit(nodec);
  while((vbuf = tcmapiternext(nodec, &vsiz)) != NULL){
    int rsiz;
    BDBNODE *node = (BDBNODE *)tcmapget(bdb->nodec, vbuf, vsiz, &rsiz);
    if(node->dirty && !tcbdbnodesave(bdb, node)) err = true;
  }
  if(clk) tcbdbunlockcache(bdb);
  tcdumpmeta(bdb);
  if(!tchdbmemsync(bdb->hdb, phys)) err = true;
  return err ? false : true;
}


/* Get the maximum number of cached leaf nodes of a B+ tree database object. */
uint32_t tcbdblmemb(TCBDB *bdb){
  assert(bdb);
  return bdb->lmemb;
}


/* Get the maximum number of cached non-leaf nodes of a B+ tree database object. */
uint32_t tcbdbnmemb(TCBDB *bdb){
  assert(bdb);
  return bdb->nmemb;
}


/* Get the number of the leaf nodes of B+ tree database object. */
uint64_t tcbdblnum(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return bdb->lnum;
}


/* Get the number of the non-leaf nodes of B+ tree database object. */
uint64_t tcbdbnnum(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return bdb->nnum;
}


/* Get the number of elements of the bucket array of a B+ tree database object. */
uint64_t tcbdbbnum(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return tchdbbnum(bdb->hdb);
}


/* Get the record alignment of a B+ tree database object. */
uint32_t tcbdbalign(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return tchdbalign(bdb->hdb);
}


/* Get the maximum number of the free block pool of a B+ tree database object. */
uint32_t tcbdbfbpmax(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return tchdbfbpmax(bdb->hdb);
}


/* Get the inode number of the database file of a B+ tree database object. */
uint64_t tcbdbinode(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return tchdbinode(bdb->hdb);
}


/* Get the modification time of the database file of a B+ tree database object. */
time_t tcbdbmtime(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return tchdbmtime(bdb->hdb);
}


/* Get the additional flags of a B+ tree database object. */
uint8_t tcbdbflags(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return tchdbflags(bdb->hdb);
}


/* Get the options of a B+ tree database object. */
uint8_t tcbdbopts(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return bdb->opts;
}


/* Get the pointer to the opaque field of a B+ tree database object. */
char *tcbdbopaque(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return tchdbopaque(bdb->hdb) + BDBOPAQUESIZ;
}


/* Get the number of used elements of the bucket array of a B+ tree database object. */
uint64_t tcbdbbnumused(TCBDB *bdb){
  assert(bdb);
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    return 0;
  }
  return tchdbbnumused(bdb->hdb);
}


/* Store a new record into a B+ tree database object with backward duplication. */
bool tcbdbputdupback(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz){
  assert(bdb && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  if(!tcbdblockmethod(bdb, true)) return false;
  if(!bdb->open || !bdb->wmode){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbputimpl(bdb, kbuf, ksiz, vbuf, vsiz, BDBPDDUPB);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Store a new string record into a B+ tree database object with backward duplication. */
bool tcbdbputdupback2(TCBDB *bdb, const char *kstr, const char *vstr){
  assert(bdb && kstr && vstr);
  return tcbdbputdupback(bdb, kstr, strlen(kstr), vstr, strlen(vstr));
}


/* Move a cursor object to the rear of records corresponding a key. */
bool tcbdbcurjumpback(BDBCUR *cur, const char *kbuf, int ksiz){
  assert(cur && kbuf && ksiz >= 0);
  TCBDB *bdb = cur->bdb;
  if(!tcbdblockmethod(bdb, false)) return false;
  if(!bdb->open){
    tchdbsetecode(bdb->hdb, TCEINVALID, __FILE__, __LINE__, __func__);
    tcbdbunlockmethod(bdb);
    return false;
  }
  bool rv = tcbdbcurjumpimpl(cur, kbuf, ksiz, false);
  tcbdbunlockmethod(bdb);
  return rv;
}


/* Compare keys of two records by lexical order. */
int tcbdbcmplexical(const char *aptr, int asiz, const char *bptr, int bsiz, void *op){
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);
  int min = asiz < bsiz ? asiz : bsiz;
  for(int i = 0; i < min; i++){
    if(((unsigned char *)aptr)[i] != ((unsigned char *)bptr)[i])
      return ((unsigned char *)aptr)[i] - ((unsigned char *)bptr)[i];
  }
  if(asiz == bsiz) return 0;
  return asiz - bsiz;
}


/* Compare two keys as decimal strings of real numbers. */
int tcbdbcmpdecimal(const char *aptr, int asiz, const char *bptr, int bsiz, void *op){
  assert(aptr && asiz >= 0 && bptr && bsiz >= 0);

  return 0;
}


/* Compare two keys as 32-bit integers in the native byte order. */
int tcbdbcmpint32(const char *aptr, int asiz, const char *bptr, int bsiz, void *op){
  assert(aptr && asiz == sizeof(int32_t) && bptr && bsiz == sizeof(int32_t));
  return *(int32_t *)aptr - *(int32_t *)bptr;
}


/* Compare two keys as 64-bit integers in the native byte order. */
int tcbdbcmpint64(const char *aptr, int asiz, const char *bptr, int bsiz, void *op){
  assert(aptr && asiz == sizeof(int64_t) && bptr && bsiz == sizeof(int64_t));
  return *(int64_t *)aptr - *(int64_t *)bptr;
}



/*************************************************************************************************
 * private features
 *************************************************************************************************/


/* Clear all members.
   `bdb' specifies the B+ tree database object. */
static void tcbdbclear(TCBDB *bdb){
  assert(bdb);
  bdb->mmtx = NULL;
  bdb->cmtx = NULL;
  bdb->tmtx = NULL;
  bdb->hdb = NULL;
  bdb->opaque = NULL;
  bdb->open = false;
  bdb->wmode = false;
  bdb->lmemb = BDBDEFLMEMB;
  bdb->nmemb = BDBDEFNMEMB;
  bdb->opts = 0;
  bdb->root = 0;
  bdb->first = 0;
  bdb->last = 0;
  bdb->lnum = 0;
  bdb->nnum = 0;
  bdb->rnum = 0;
  bdb->leafc = NULL;
  bdb->nodec = NULL;
  bdb->cmp = tcbdbcmplexical;
  bdb->cmpop = NULL;
  bdb->lcnum = BDBDEFLCNUM;
  bdb->ncnum = BDBDEFNCNUM;
  bdb->hist = NULL;
  bdb->hnum = 0;
  bdb->hleaf = 0;
  bdb->lleaf = 0;
  bdb->tran = false;
  bdb->rbopaque = NULL;
  bdb->cnt_saveleaf = -1;
  bdb->cnt_loadleaf = -1;
  bdb->cnt_adjleafc = -1;
  bdb->cnt_savenode = -1;
  bdb->cnt_loadnode = -1;
  bdb->cnt_adjnodec = -1;
  TCDODEBUG(bdb->cnt_saveleaf = 0);
  TCDODEBUG(bdb->cnt_loadleaf = 0);
  TCDODEBUG(bdb->cnt_adjleafc = 0);
  TCDODEBUG(bdb->cnt_savenode = 0);
  TCDODEBUG(bdb->cnt_loadnode = 0);
  TCDODEBUG(bdb->cnt_adjnodec = 0);
}


/* Serialize meta data into the opaque field.
   `bdb' specifies the B+ tree database object. */
static void tcdumpmeta(TCBDB *bdb){
  assert(bdb);
  char *wp = bdb->opaque;
  uint32_t lnum;
  lnum = bdb->lmemb;
  lnum = TCHTOIL(lnum);
  memcpy(wp, &lnum, sizeof(lnum));
  wp += sizeof(lnum);
  lnum = bdb->nmemb;
  lnum = TCHTOIL(lnum);
  memcpy(wp, &lnum, sizeof(lnum));
  wp += sizeof(lnum);
  uint64_t llnum;
  llnum = bdb->root;
  llnum = TCHTOILL(llnum);
  memcpy(wp, &llnum, sizeof(llnum));
  wp += sizeof(llnum);
  llnum = bdb->first;
  llnum = TCHTOILL(llnum);
  memcpy(wp, &llnum, sizeof(llnum));
  wp += sizeof(llnum);
  llnum = bdb->last;
  llnum = TCHTOILL(llnum);
  memcpy(wp, &llnum, sizeof(llnum));
  wp += sizeof(llnum);
  llnum = bdb->lnum;
  llnum = TCHTOILL(llnum);
  memcpy(wp, &llnum, sizeof(llnum));
  wp += sizeof(llnum);
  llnum = bdb->nnum;
  llnum = TCHTOILL(llnum);
  memcpy(wp, &llnum, sizeof(llnum));
  wp += sizeof(llnum);
  llnum = bdb->rnum;
  llnum = TCHTOILL(llnum);
  memcpy(wp, &llnum, sizeof(llnum));
  wp += sizeof(llnum);
}


/* Deserialize meta data from the opaque field.
   `bdb' specifies the B+ tree database object. */
static void tcloadmeta(TCBDB *bdb){
  const char *rp = bdb->opaque;
  uint32_t lnum;
  memcpy(&lnum, rp, sizeof(lnum));
  rp += sizeof(lnum);
  bdb->lmemb = TCITOHL(lnum);
  memcpy(&lnum, rp, sizeof(lnum));
  rp += sizeof(lnum);
  bdb->nmemb = TCITOHL(lnum);
  uint64_t llnum;
  memcpy(&llnum, rp, sizeof(llnum));
  bdb->root = TCITOHLL(llnum);
  rp += sizeof(llnum);
  memcpy(&llnum, rp, sizeof(llnum));
  bdb->first = TCITOHLL(llnum);
  rp += sizeof(llnum);
  memcpy(&llnum, rp, sizeof(llnum));
  bdb->last = TCITOHLL(llnum);
  rp += sizeof(llnum);
  memcpy(&llnum, rp, sizeof(llnum));
  bdb->lnum = TCITOHLL(llnum);
  rp += sizeof(llnum);
  memcpy(&llnum, rp, sizeof(llnum));
  bdb->nnum = TCITOHLL(llnum);
  rp += sizeof(llnum);
  memcpy(&llnum, rp, sizeof(llnum));
  bdb->rnum = TCITOHLL(llnum);
  rp += sizeof(llnum);
}


/* Create a new leaf.
   `bdb' specifies the B+ tree database object.
   `prev' specifies the ID number of the previous leaf.
   `next' specifies the ID number of the next leaf.
   The return value is the new leaf object. */
static BDBLEAF *tcbdbleafnew(TCBDB *bdb, uint64_t prev, uint64_t next){
  assert(bdb);
  BDBLEAF lent;
  lent.id = ++bdb->lnum;
  lent.recs = tclistnew2(bdb->lmemb + 1);
  lent.prev = prev;
  lent.next = next;
  lent.dirty = true;
  tcmapputkeep(bdb->leafc, &(lent.id), sizeof(lent.id), &lent, sizeof(lent));
  int rsiz;
  return (BDBLEAF *)tcmapget(bdb->leafc, &(lent.id), sizeof(lent.id), &rsiz);
}


/* Remove a leaf from the cache.
   `bdb' specifies the B+ tree database object.
   `id' specifies the ID number of the leaf.
   If successful, the return value is true, else, it is false. */
static bool tcbdbleafcacheout(TCBDB *bdb, uint64_t id){
  assert(bdb && id > 0);
  int rsiz;
  BDBLEAF *leaf = (BDBLEAF *)tcmapget(bdb->leafc, &id, sizeof(id), &rsiz);
  if(!leaf){
    tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
    return false;
  }
  bool err = false;
  if(leaf->dirty && !tcbdbleafsave(bdb, leaf)) err = true;
  TCLIST *recs = leaf->recs;
  int ln = tclistnum(recs);
  for(int i = 0; i < ln; i++){
    BDBREC *recp = (BDBREC *)tclistval(recs, i, &rsiz);
    free(recp->kbuf);
    free(recp->vbuf);
    if(recp->rest) tclistdel(recp->rest);
  }
  tclistdel(recs);
  tcmapout(bdb->leafc, &id, sizeof(id));
  return err ? false : true;
}


/* Save a leaf into the internal database.
   `bdb' specifies the B+ tree database object.
   `leaf' specifies the leaf object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbleafsave(TCBDB *bdb, BDBLEAF *leaf){
  assert(bdb && leaf);
  TCDODEBUG(bdb->cnt_saveleaf++);
  TCXSTR *rbuf = tcxstrnew3(BDBPAGEBUFSIZ);
  char hbuf[(sizeof(uint64_t)+1)*3];
  char *wp = hbuf;
  uint64_t llnum;
  int step;
  llnum = leaf->prev;
  TC_SETVNUMBUF64(step, wp, llnum);
  wp += step;
  llnum = leaf->next;
  TC_SETVNUMBUF64(step, wp, llnum);
  wp += step;
  tcxstrcat(rbuf, hbuf, wp - hbuf);
  TCLIST *recs = leaf->recs;
  int ln = tclistnum(recs);
  for(int i = 0; i < ln; i++){
    int rsiz;
    BDBREC *recp = (BDBREC *)tclistval(recs, i, &rsiz);
    int lnum;
    wp = hbuf;
    lnum = recp->ksiz;
    TC_SETVNUMBUF(step, wp, lnum);
    wp += step;
    lnum = recp->vsiz;
    TC_SETVNUMBUF(step, wp, lnum);
    wp += step;
    TCLIST *rest = recp->rest;
    int rnum = rest ? tclistnum(rest) : 0;
    TC_SETVNUMBUF(step, wp, rnum);
    wp += step;
    tcxstrcat(rbuf, hbuf, wp - hbuf);
    tcxstrcat(rbuf, recp->kbuf, recp->ksiz);
    tcxstrcat(rbuf, recp->vbuf, recp->vsiz);
    for(int j = 0; j < rnum; j++){
      int vsiz;
      const char *vbuf = tclistval(rest, j, &vsiz);
      TC_SETVNUMBUF(step, hbuf, vsiz);
      tcxstrcat(rbuf, hbuf, step);
      tcxstrcat(rbuf, vbuf, vsiz);
    }
  }
  bool err = false;
  step = sprintf(hbuf, "%llx", leaf->id);
  if(!tchdbput(bdb->hdb, hbuf, step, tcxstrptr(rbuf), tcxstrsize(rbuf))) err = true;
  tcxstrdel(rbuf);
  leaf->dirty = false;
  return err ? false : true;
}


/* Load a leaf from the internal database.
   `bdb' specifies the B+ tree database object.
   `id' specifies the ID number of the leaf.
   The return value is the leaf object or `NULL' on failure. */
static BDBLEAF *tcbdbleafload(TCBDB *bdb, uint64_t id){
  assert(bdb && id > 0);
  bool clk = tcbdblockcache(bdb);
  int rsiz;
  BDBLEAF *leaf = (BDBLEAF *)tcmapget(bdb->leafc, &id, sizeof(id), &rsiz);
  if(leaf){
    tcmapmove(bdb->leafc, &id, sizeof(id), false);
    if(clk) tcbdbunlockcache(bdb);
    return leaf;
  }
  if(clk) tcbdbunlockcache(bdb);
  TCDODEBUG(bdb->cnt_loadleaf++);
  char hbuf[(sizeof(uint64_t)+1)*3];
  int step;
  step = sprintf(hbuf, "%llx", id);
  char *rbuf = NULL;
  char wbuf[BDBPAGEBUFSIZ];
  const char *rp = NULL;
  rsiz = tchdbget3(bdb->hdb, hbuf, step, wbuf, BDBPAGEBUFSIZ);
  if(rsiz < 1){
    tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
    return false;
  } else if(rsiz < BDBPAGEBUFSIZ){
    rp = wbuf;
  } else {
    if(!(rbuf = tchdbget(bdb->hdb, hbuf, step, &rsiz))){
      tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
      return false;
    }
    rp = rbuf;
  }
  BDBLEAF lent;
  lent.id = id;
  uint64_t llnum;
  TC_READVNUMBUF64(rp, llnum, step);
  lent.prev = llnum;
  rp += step;
  rsiz -= step;
  TC_READVNUMBUF64(rp, llnum, step);
  lent.next = llnum;
  rp += step;
  rsiz -= step;
  lent.dirty = false;
  lent.recs = tclistnew2(bdb->lmemb + 1);
  bool err = false;
  while(rsiz >= 3){
    BDBREC rec;
    TC_READVNUMBUF(rp, rec.ksiz, step);
    rp += step;
    rsiz -= step;
    TC_READVNUMBUF(rp, rec.vsiz, step);
    rp += step;
    rsiz -= step;
    int rnum;
    TC_READVNUMBUF(rp, rnum, step);
    rp += step;
    rsiz -= step;
    if(rsiz < rec.ksiz + rec.vsiz + rnum){
      err = true;
      break;
    }
    rec.kbuf = tcmemdup(rp, rec.ksiz);
    rp += rec.ksiz;
    rsiz -= rec.ksiz;
    rec.vbuf = tcmemdup(rp, rec.vsiz);
    rp += rec.vsiz;
    rsiz -= rec.vsiz;
    if(rnum > 0){
      rec.rest = tclistnew2(rnum);
      while(rnum-- > 0 && rsiz > 0){
        int vsiz;
        TC_READVNUMBUF(rp, vsiz, step);
        rp += step;
        rsiz -= step;
        if(rsiz < vsiz){
          err = true;
          break;
        }
        tclistpush(rec.rest, rp, vsiz);
        rp += vsiz;
        rsiz -= vsiz;
      }
    } else {
      rec.rest = NULL;
    }
    tclistpush(lent.recs, &rec, sizeof(rec));
  }
  free(rbuf);
  if(err || rsiz != 0){
    tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
    return NULL;
  }
  clk = tcbdblockcache(bdb);
  tcmapputkeep(bdb->leafc, &(lent.id), sizeof(lent.id), &lent, sizeof(lent));
  leaf = (BDBLEAF *)tcmapget(bdb->leafc, &(lent.id), sizeof(lent.id), &rsiz);
  if(clk) tcbdbunlockcache(bdb);
  return leaf;
}


/* Load the historical leaf from the internal database.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is the pointer to the leaf, else, it is `NULL'. */
static BDBLEAF *tcbdbgethistleaf(TCBDB *bdb, const char *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  BDBLEAF *leaf = tcbdbleafload(bdb, bdb->hleaf);
  if(!leaf) return NULL;
  int ln = tclistnum(leaf->recs);
  if(ln < 2) return NULL;
  int rsiz;
  BDBREC *recp = (BDBREC *)tclistval(leaf->recs, 0, &rsiz);
  int rv = bdb->cmp(kbuf, ksiz, recp->kbuf, recp->ksiz, bdb->cmpop);
  if(rv == 0) return leaf;
  if(rv < 0) return NULL;
  recp = (BDBREC *)tclistval(leaf->recs, ln - 1, &rsiz);
  rv = bdb->cmp(kbuf, ksiz, recp->kbuf, recp->ksiz, bdb->cmpop);
  if(rv <= 0 || leaf->next < 1) return leaf;
  return NULL;
}


/* Add a record to a leaf.
   `bdb' specifies the B+ tree database object.
   `leaf' specifies the leaf object.
   `dmode' specifies behavior when the key overlaps.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   If successful, the return value is true, else, it is false. */
static bool tcbdbleafaddrec(TCBDB *bdb, BDBLEAF *leaf, int dmode,
                            const char *kbuf, int ksiz, const char *vbuf, int vsiz){
  assert(bdb && leaf && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  BDBCMP cmp = bdb->cmp;
  void *cmpop = bdb->cmpop;
  TCLIST *recs = leaf->recs;
  int ln = tclistnum(recs);
  int left = 0;
  int right = ln;
  int i = (left + right) / 2;
  while(right >= left && i < ln){
    int rsiz;
    BDBREC *recp = (BDBREC *)tclistval(recs, i, &rsiz);
    int rv = cmp(kbuf, ksiz, recp->kbuf, recp->ksiz, cmpop);
    if(rv == 0){
      break;
    } else if(rv <= 0){
      right = i - 1;
    } else {
      left = i + 1;
    }
    i = (left + right) / 2;
  }
  while(i < ln){
    int rsiz;
    BDBREC *recp = (BDBREC *)tclistval(recs, i, &rsiz);
    int rv = cmp(kbuf, ksiz, recp->kbuf, recp->ksiz, cmpop);
    if(rv == 0){
      switch(dmode){
      case BDBPDKEEP:
        return false;
      case BDBPDCAT:
        recp->vbuf = tcrealloc(recp->vbuf, recp->vsiz + vsiz + 1);
        memcpy(recp->vbuf + recp->vsiz, vbuf, vsiz);
        recp->vsiz += vsiz;
        recp->vbuf[recp->vsiz] = '\0';
        break;
      case BDBPDDUP:
        if(!recp->rest) recp->rest = tclistnew();
        tclistpush(recp->rest, vbuf, vsiz);
        bdb->rnum++;
        break;
      case BDBPDDUPB:
        if(!recp->rest) recp->rest = tclistnew();
        tclistunshift(recp->rest, recp->vbuf, recp->vsiz);
        if(vsiz > recp->vsiz) recp->vbuf = tcrealloc(recp->vbuf, vsiz + 1);
        memcpy(recp->vbuf, vbuf, vsiz);
        recp->vbuf[vsiz] = '\0';
        recp->vsiz = vsiz;
        bdb->rnum++;
        break;
      default:
        if(vsiz > recp->vsiz) recp->vbuf = tcrealloc(recp->vbuf, vsiz + 1);
        memcpy(recp->vbuf, vbuf, vsiz);
        recp->vbuf[vsiz] = '\0';
        recp->vsiz = vsiz;
        break;
      }
      break;
    } else if(rv < 0){
      BDBREC rec;
      rec.kbuf = tcmemdup(kbuf, ksiz);
      rec.ksiz = ksiz;
      rec.vbuf = tcmemdup(vbuf, vsiz);
      rec.vsiz = vsiz;
      rec.rest = NULL;
      tclistinsert(recs, i, &rec, sizeof(rec));
      bdb->rnum++;
      break;
    }
    i++;
  }
  if(i >= ln){
    BDBREC rec;
    rec.kbuf = tcmemdup(kbuf, ksiz);
    rec.ksiz = ksiz;
    rec.vbuf = tcmemdup(vbuf, vsiz);
    rec.vsiz = vsiz;
    rec.rest = NULL;
    tclistpush(recs, &rec, sizeof(rec));
    bdb->rnum++;
  }
  leaf->dirty = true;
  return true;
}


/* Calculate the size of data of a leaf object.
   `bdb' specifies the B+ tree database object.
   `leaf' specifies the leaf object.
   The return value is size of data of the leaf. */
static int tcbdbleafdatasize(BDBLEAF *leaf){
  assert(leaf);
  int sum = 0;
  TCLIST *recs = leaf->recs;
  int ln = tclistnum(recs);
  for(int i = 0; i < ln; i++){
    int rsiz;
    BDBREC *recp = (BDBREC *)tclistval(recs, i, &rsiz);
    sum += recp->ksiz + recp->vsiz;
    if(recp->rest){
      TCLIST *rest = recp->rest;
      int rnum = tclistnum(rest);
      for(int j = 0; j < rnum; j++){
        int vsiz;
        tclistval(rest, j, &vsiz);
        sum += vsiz;
      }
    }
  }
  return sum;
}


/* Divide a leaf into two.
   `bdb' specifies the B+ tree database object.
   `leaf' specifies the leaf object.
   The return value is the new leaf object or `NULL' on failure. */
static BDBLEAF *tcbdbleafdivide(TCBDB *bdb, BDBLEAF *leaf){
  assert(bdb && leaf);
  bdb->hleaf = 0;
  TCLIST *recs = leaf->recs;
  int mid = tclistnum(recs) / 2;
  int rsiz;
  BDBREC *recp = (BDBREC *)tclistval(recs, mid, &rsiz);
  BDBLEAF *newleaf = tcbdbleafnew(bdb, leaf->id, leaf->next);
  if(newleaf->next > 0){
    BDBLEAF *nextleaf = tcbdbleafload(bdb, newleaf->next);
    if(!nextleaf) return NULL;
    nextleaf->prev = newleaf->id;
    nextleaf->dirty = true;
  }
  leaf->next = newleaf->id;
  leaf->dirty = true;
  int ln = tclistnum(recs);
  TCLIST *newrecs = newleaf->recs;
  for(int i = mid; i < ln; i++){
    recp = (BDBREC *)tclistval(recs, i, &rsiz);
    tclistpush(newrecs, recp, sizeof(*recp));
  }
  ln = tclistnum(newrecs);
  for(int i = 0; i < ln; i++){
    free(tclistpop(recs, &rsiz));
  }
  return newleaf;
}


/* Create a new node.
   `bdb' specifies the B+ tree database object.
   `heir' specifies the ID of the child before the first index.
   The return value is the new node object. */
static BDBNODE *tcbdbnodenew(TCBDB *bdb, uint64_t heir){
  assert(bdb && heir > 0);
  BDBNODE nent;
  nent.id = ++bdb->nnum + BDBNODEIDBASE;
  nent.idxs = tclistnew2(bdb->nmemb + 1);
  nent.heir = heir;
  nent.dirty = true;
  tcmapputkeep(bdb->nodec, &(nent.id), sizeof(nent.id), &nent, sizeof(nent));
  int rsiz;
  return (BDBNODE *)tcmapget(bdb->nodec, &(nent.id), sizeof(nent.id), &rsiz);
}


/* Remove a node from the cache.
   `bdb' specifies the B+ tree database object.
   `id' specifies the ID number of the node.
   If successful, the return value is true, else, it is false. */
static bool tcbdbnodecacheout(TCBDB *bdb, uint64_t id){
  assert(bdb && id > BDBNODEIDBASE);
  int rsiz;
  BDBNODE *node = (BDBNODE *)tcmapget(bdb->nodec, &id, sizeof(id), &rsiz);
  if(!node){
    tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
    return false;
  }
  bool err = false;
  if(node->dirty && !tcbdbnodesave(bdb, node)) err = true;
  TCLIST *idxs = node->idxs;
  int ln = tclistnum(idxs);
  for(int i = 0; i < ln; i++){
    BDBIDX *idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
    free(idxp->kbuf);
  }
  tclistdel(idxs);
  tcmapout(bdb->nodec, &id, sizeof(id));
  return err ? false : true;
}


/* Save a node into the internal database.
   `bdb' specifies the B+ tree database object.
   `node' specifies the node object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbnodesave(TCBDB *bdb, BDBNODE *node){
  assert(bdb && node);
  TCDODEBUG(bdb->cnt_savenode++);
  TCXSTR *rbuf = tcxstrnew3(BDBPAGEBUFSIZ);
  char hbuf[(sizeof(uint64_t)+1)*2];
  uint64_t llnum;
  int step;
  llnum = node->heir;
  TC_SETVNUMBUF64(step, hbuf, llnum);
  tcxstrcat(rbuf, hbuf, step);
  TCLIST *idxs = node->idxs;
  int ln = tclistnum(idxs);
  for(int i = 0; i < ln; i++){
    int rsiz;
    BDBIDX *idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
    char *wp = hbuf;
    llnum = idxp->pid;
    TC_SETVNUMBUF64(step, wp, llnum);
    wp += step;
    uint32_t lnum = idxp->ksiz;
    TC_SETVNUMBUF(step, wp, lnum);
    wp += step;
    tcxstrcat(rbuf, hbuf, wp - hbuf);
    tcxstrcat(rbuf, idxp->kbuf, idxp->ksiz);
  }
  bool err = false;
  step = sprintf(hbuf, "#%llx", node->id - BDBNODEIDBASE);
  if(!tchdbput(bdb->hdb, hbuf, step, tcxstrptr(rbuf), tcxstrsize(rbuf))) err = true;
  tcxstrdel(rbuf);
  node->dirty = false;
  return err ? false : true;
}


/* Load a node from the internal database.
   `bdb' specifies the B+ tree database object.
   `id' specifies the ID number of the node.
   The return value is the node object or `NULL' on failure. */
static BDBNODE *tcbdbnodeload(TCBDB *bdb, uint64_t id){
  assert(bdb && id > BDBNODEIDBASE);
  bool clk = tcbdblockcache(bdb);
  int rsiz;
  BDBNODE *node = (BDBNODE *)tcmapget(bdb->nodec, &id, sizeof(id), &rsiz);
  if(node){
    tcmapmove(bdb->nodec, &id, sizeof(id), false);
    if(clk) tcbdbunlockcache(bdb);
    return node;
  }
  if(clk) tcbdbunlockcache(bdb);
  TCDODEBUG(bdb->cnt_loadnode++);
  char hbuf[(sizeof(uint64_t)+1)*2];
  int step;
  step = sprintf(hbuf, "#%llx", id - BDBNODEIDBASE);
  char *rbuf = NULL;
  char wbuf[BDBPAGEBUFSIZ];
  const char *rp = NULL;
  rsiz = tchdbget3(bdb->hdb, hbuf, step, wbuf, BDBPAGEBUFSIZ);
  if(rsiz < 1){
    tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
    return false;
  } else if(rsiz < BDBPAGEBUFSIZ){
    rp = wbuf;
  } else {
    if(!(rbuf = tchdbget(bdb->hdb, hbuf, step, &rsiz))){
      tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
      return false;
    }
    rp = rbuf;
  }
  BDBNODE nent;
  nent.id = id;
  uint64_t llnum;
  TC_READVNUMBUF64(rp, llnum, step);
  nent.heir = llnum;
  rp += step;
  rsiz -= step;
  nent.dirty = false;
  nent.idxs = tclistnew2(bdb->nmemb + 1);
  bool err = false;
  while(rsiz >= 2){
    BDBIDX idx;
    TC_READVNUMBUF64(rp, idx.pid, step);
    rp += step;
    rsiz -= step;
    TC_READVNUMBUF(rp, idx.ksiz, step);
    rp += step;
    rsiz -= step;
    if(rsiz < idx.ksiz){
      err = true;
      break;
    }
    idx.kbuf = tcmemdup(rp, idx.ksiz);
    rp += idx.ksiz;
    rsiz -= idx.ksiz;
    tclistpush(nent.idxs, &idx, sizeof(idx));
  }
  free(rbuf);
  if(err || rsiz != 0){
    tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
    return NULL;
  }
  clk = tcbdblockcache(bdb);
  tcmapputkeep(bdb->nodec, &(nent.id), sizeof(nent.id), &nent, sizeof(nent));
  node = (BDBNODE *)tcmapget(bdb->nodec, &(nent.id), sizeof(nent.id), &rsiz);
  if(clk) tcbdbunlockcache(bdb);
  return node;
}


/* Add an index to a node.
   `bdb' specifies the B+ tree database object.
   `node' specifies the node object.
   `order' specifies whether the calling sequence is orderd or not.
   `pid' specifies the ID number of referred page.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is true, else, it is false. */
static void tcbdbnodeaddidx(TCBDB *bdb, BDBNODE *node, bool order, uint64_t pid,
                            const char *kbuf, int ksiz){
  assert(bdb && node && pid > 0 && kbuf && ksiz >= 0);
  BDBIDX idx;
  idx.pid = pid;
  idx.kbuf = tcmemdup(kbuf, ksiz);
  idx.ksiz = ksiz;
  BDBCMP cmp = bdb->cmp;
  void *cmpop = bdb->cmpop;
  TCLIST *idxs = node->idxs;
  if(order){
    tclistpush(idxs, &idx, sizeof(idx));
  } else {
    int ln = tclistnum(idxs);
    int left = 0;
    int right = ln;
    int i = (left + right) / 2;
    while(right >= left && i < ln){
      int rsiz;
      BDBIDX *idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
      int rv = cmp(kbuf, ksiz, idxp->kbuf, idxp->ksiz, cmpop);
      if(rv == 0){
        break;
      } else if(rv <= 0){
        right = i - 1;
      } else {
        left = i + 1;
      }
      i = (left + right) / 2;
    }
    while(i < ln){
      int rsiz;
      BDBIDX *idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
      if(cmp(kbuf, ksiz, idxp->kbuf, idxp->ksiz, cmpop) < 0){
        tclistinsert(idxs, i, &idx, sizeof(idx));
        break;
      }
      i++;
    }
    if(i >= ln) tclistpush(idxs, &idx, sizeof(idx));
  }
  node->dirty = true;
}


/* Search the leaf object corresponding to a key.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   The return value is the ID number of the leaf object or 0 on failure. */
static uint64_t tcbdbsearchleaf(TCBDB *bdb, const char *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  BDBCMP cmp = bdb->cmp;
  void *cmpop = bdb->cmpop;
  uint64_t *hist = bdb->hist;
  uint64_t pid = bdb->root;
  int hnum = 0;
  bdb->hleaf = 0;
  while(pid > BDBNODEIDBASE){
    BDBNODE *node = tcbdbnodeload(bdb, pid);
    if(!node){
      tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
      return 0;
    }
    TCLIST *idxs = node->idxs;
    int ln = tclistnum(idxs);
    if(ln < 1){
      tchdbsetecode(bdb->hdb, TCEMISC, __FILE__, __LINE__, __func__);
      return 0;
    }
    hist[hnum++] = node->id;
    int left = 0;
    int right = ln;
    int i = (left + right) / 2;
    BDBIDX *idxp = NULL;
    while(right >= left && i < ln){
      int rsiz;
      idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
      int rv = cmp(kbuf, ksiz, idxp->kbuf, idxp->ksiz, cmpop);
      if(rv == 0){
        break;
      } else if(rv <= 0){
        right = i - 1;
      } else {
        left = i + 1;
      }
      i = (left + right) / 2;
    }
    if(i > 0) i--;
    while(i < ln){
      int rsiz;
      idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
      if(cmp(kbuf, ksiz, idxp->kbuf, idxp->ksiz, cmpop) < 0){
        if(i == 0){
          pid = node->heir;
          break;
        }
        idxp = (BDBIDX *)tclistval(idxs, i - 1, &rsiz);
        pid = idxp->pid;
        break;
      }
      i++;
    }
    if(i >= ln) pid = idxp->pid;
  }
  if(bdb->lleaf == pid) bdb->hleaf = pid;
  bdb->lleaf = pid;
  bdb->hnum = hnum;
  return pid;
}


/* Search a record of a leaf.
   `bdb' specifies the B+ tree database object.
   `leaf' specifies the leaf object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `ip' specifies the pointer to a variable to fetch the index of the correspnding record.
   The return value is the pointer to a corresponding record or `NULL' on failure. */
static BDBREC *tcbdbsearchrec(TCBDB *bdb, BDBLEAF *leaf, const char *kbuf, int ksiz, int *ip){
  assert(bdb && leaf && kbuf && ksiz >= 0);
  BDBCMP cmp = bdb->cmp;
  void *cmpop = bdb->cmpop;
  TCLIST *recs = leaf->recs;
  int ln = tclistnum(recs);
  int left = 0;
  int right = ln;
  int i = (left + right) / 2;
  while(right >= left && i < ln){
    int rsiz;
    BDBREC *recp = (BDBREC *)tclistval(recs, i, &rsiz);
    int rv = cmp(kbuf, ksiz, recp->kbuf, recp->ksiz, cmpop);
    if(rv == 0){
      if(ip) *ip = i;
      return recp;
    } else if(rv <= 0){
      right = i - 1;
    } else {
      left = i + 1;
    }
    i = (left + right) / 2;
  }
  if(ip) *ip = i;
  return NULL;
}


/* Adjust the caches for leaves and nodes.
   `bdb' specifies the B+ tree database object.
   The return value is true if successful, else, it is false. */
static bool tcbdbcacheadjust(TCBDB *bdb){
  bool err = false;
  if(tcmaprnum(bdb->leafc) > bdb->lcnum){
    TCDODEBUG(bdb->cnt_adjleafc++);
    bool clk = tcbdblockcache(bdb);
    tcmapiterinit(bdb->leafc);
    for(int i = 0; i < BDBCACHEOUT; i++){
      int rsiz;
      const char *tmp = tcmapiternext(bdb->leafc, &rsiz);
      if(!tcbdbleafcacheout(bdb, *(uint64_t *)tmp)) err = true;
    }
    if(clk) tcbdbunlockcache(bdb);
  }
  if(tcmaprnum(bdb->nodec) > bdb->ncnum){
    TCDODEBUG(bdb->cnt_adjnodec++);
    bool clk = tcbdblockcache(bdb);
    tcmapiterinit(bdb->nodec);
    for(int i = 0; i < BDBCACHEOUT; i++){
      int rsiz;
      const char *tmp = tcmapiternext(bdb->nodec, &rsiz);
      if(!tcbdbnodecacheout(bdb, *(uint64_t *)tmp)) err = true;
    }
    if(clk) tcbdbunlockcache(bdb);
  }
  return err ? false : true;
}


/* Purge dirty pages of caches for leaves and nodes.
   `bdb' specifies the B+ tree database object. */
static void tcbdbcachepurge(TCBDB *bdb){
  bool clk = tcbdblockcache(bdb);
  int tsiz;
  const char *tmp;
  tcmapiterinit(bdb->leafc);
  while((tmp = tcmapiternext(bdb->leafc, &tsiz)) != NULL){
    int lsiz;
    BDBLEAF *leaf = (BDBLEAF *)tcmapiterval(tmp, &lsiz);
    if(!leaf->dirty) continue;
    TCLIST *recs = leaf->recs;
    int ln = tclistnum(recs);
    for(int i = 0; i < ln; i++){
      int rsiz;
      BDBREC *recp = (BDBREC *)tclistval(recs, i, &rsiz);
      free(recp->kbuf);
      free(recp->vbuf);
      if(recp->rest) tclistdel(recp->rest);
    }
    tclistdel(recs);
    tcmapout(bdb->leafc, tmp, tsiz);
  }
  tcmapiterinit(bdb->nodec);
  while((tmp = tcmapiternext(bdb->nodec, &tsiz)) != NULL){
    int nsiz;
    BDBNODE *node = (BDBNODE *)tcmapiterval(tmp, &nsiz);
    if(!node->dirty) continue;
    TCLIST *idxs = node->idxs;
    int ln = tclistnum(idxs);
    for(int i = 0; i < ln; i++){
      int rsiz;
      BDBIDX *idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
      free(idxp->kbuf);
    }
    tclistdel(idxs);
    tcmapout(bdb->nodec, tmp, tsiz);
  }
  if(clk) tcbdbunlockcache(bdb);
}


/* Open a database file and connect a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `path' specifies the path of the internal database file.
   `omode' specifies the connection mode.
   If successful, the return value is true, else, it is false. */
static bool tcbdbopenimpl(TCBDB *bdb, const char *path, int omode){
  assert(bdb && path);
  int homode = HDBOREADER;
  if(omode & BDBOWRITER){
    homode = HDBOWRITER;
    if(omode & BDBOCREAT) homode |= HDBOCREAT;
    if(omode & BDBOTRUNC) homode |= HDBOTRUNC;
    bdb->wmode = true;
  } else {
    bdb->wmode = false;
  }
  if(omode & BDBONOLCK) homode |= HDBONOLCK;
  if(omode & BDBOLCKNB) homode |= HDBOLCKNB;
  tchdbsettype(bdb->hdb, HDBTBTREE);
  if(!tchdbopen(bdb->hdb, path, omode)) return false;
  bdb->opaque = tchdbopaque(bdb->hdb);
  bdb->leafc = tcmapnew2(bdb->lcnum * 2 + 1);
  bdb->nodec = tcmapnew2(bdb->ncnum * 2 + 1);
  if(bdb->wmode && tchdbrnum(bdb->hdb) < 1){
    BDBLEAF *leaf = tcbdbleafnew(bdb, 0, 0);
    bdb->root = leaf->id;
    bdb->first = leaf->id;
    bdb->last = leaf->id;
    bdb->lnum = 1;
    bdb->nnum = 0;
    bdb->rnum = 0;
    tcdumpmeta(bdb);
  }
  tcloadmeta(bdb);
  if(bdb->lmemb < BDBMINLMEMB || bdb->nmemb < BDBMINNMEMB ||
     bdb->root < 1 || bdb->first < 1 || bdb->last < 1 ||
     bdb->lnum < 0 || bdb->nnum < 0 || bdb->rnum < 0){
    tchdbsetecode(bdb->hdb, TCEMETA, __FILE__, __LINE__, __func__);
    tcmapdel(bdb->nodec);
    tcmapdel(bdb->leafc);
    tchdbclose(bdb->hdb);
    return false;
  }
  bdb->open = true;
  uint8_t hopts = tchdbopts(bdb->hdb);
  uint8_t opts = 0;
  if(hopts & HDBTLARGE) opts |= BDBTLARGE;
  if(hopts & HDBTDEFLATE) opts |= BDBTDEFLATE;
  if(hopts & HDBTTCBS) opts |= BDBTTCBS;
  bdb->opts = opts;
  bdb->hleaf = 0;
  bdb->lleaf = 0;
  bdb->tran = false;
  bdb->rbopaque = NULL;
  return true;
}


/* Close a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcloseimpl(TCBDB *bdb){
  assert(bdb);
  if(bdb->tran){
    tcbdbcachepurge(bdb);
    memcpy(bdb->opaque, bdb->rbopaque, BDBOPAQUESIZ);
    tcloadmeta(bdb);
    free(bdb->rbopaque);
    bdb->tran = false;
    bdb->rbopaque = NULL;
    tcbdbunlocktran(bdb);
  }
  bool err = false;
  bdb->open = false;
  const char *vbuf;
  int vsiz;
  TCMAP *leafc = bdb->leafc;
  tcmapiterinit(leafc);
  while((vbuf = tcmapiternext(leafc, &vsiz)) != NULL){
    if(!tcbdbleafcacheout(bdb, *(uint64_t *)vbuf)) err = true;
  }
  TCMAP *nodec = bdb->nodec;
  tcmapiterinit(nodec);
  while((vbuf = tcmapiternext(nodec, &vsiz)) != NULL){
    if(!tcbdbnodecacheout(bdb, *(uint64_t *)vbuf)) err = true;
  }
  if(bdb->wmode) tcdumpmeta(bdb);
  tcmapdel(bdb->nodec);
  tcmapdel(bdb->leafc);
  if(!tchdbclose(bdb->hdb)) err = true;
  return err ? false : true;
}


/* Store a record into a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   `dmode' specifies behavior when the key overlaps.
   If successful, the return value is true, else, it is false. */
static bool tcbdbputimpl(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz,
                         int dmode){
  assert(bdb && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  BDBLEAF *leaf = NULL;
  if(bdb->hleaf < 1 || !(leaf = tcbdbgethistleaf(bdb, kbuf, ksiz))){
    uint64_t pid = tcbdbsearchleaf(bdb, kbuf, ksiz);
    if(pid < 1) return false;
    if(!(leaf = tcbdbleafload(bdb, pid))) return false;
  }
  if(!tcbdbleafaddrec(bdb, leaf, dmode, kbuf, ksiz, vbuf, vsiz)){
    tchdbsetecode(bdb->hdb, TCEKEEP, __FILE__, __LINE__, __func__);
    return false;
  }
  int rnum = tclistnum(leaf->recs);
  if(rnum > bdb->lmemb ||
     (rnum > BDBMINLMEMB && (rnum & (0x20 - 1)) == 0 &&
      tcbdbleafdatasize(leaf) >= BDBPAGEBUFSIZ)){
    BDBLEAF *newleaf = tcbdbleafdivide(bdb, leaf);
    if(!newleaf) return false;
    if(leaf->id == bdb->last) bdb->last = newleaf->id;
    uint64_t heir = leaf->id;
    uint64_t pid = newleaf->id;
    int rsiz;
    BDBREC *recp = (BDBREC *)tclistval(newleaf->recs, 0, &rsiz);
    int ksiz = recp->ksiz;
    char *kbuf = tcmemdup(recp->kbuf, ksiz);
    while(true){
      BDBNODE *node;
      if(bdb->hnum < 1){
        node = tcbdbnodenew(bdb, heir);
        tcbdbnodeaddidx(bdb, node, true, pid, kbuf, ksiz);
        bdb->root = node->id;
        free(kbuf);
        break;
      }
      uint64_t parent = bdb->hist[--bdb->hnum];
      if(!(node = tcbdbnodeload(bdb, parent))){
        free(kbuf);
        return false;
      }
      tcbdbnodeaddidx(bdb, node, false, pid, kbuf, ksiz);
      free(kbuf);
      TCLIST *idxs = node->idxs;
      int ln = tclistnum(idxs);
      if(ln <= bdb->nmemb) break;
      int mid = ln / 2;
      BDBIDX *idxp = (BDBIDX *)tclistval(idxs, mid, &rsiz);
      BDBNODE *newnode = tcbdbnodenew(bdb, idxp->pid);
      heir = node->id;
      pid = newnode->id;
      kbuf = tcmemdup(idxp->kbuf, idxp->ksiz);
      ksiz = idxp->ksiz;
      for(int i = mid + 1; i < ln; i++){
        idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
        tcbdbnodeaddidx(bdb, newnode, true, idxp->pid, idxp->kbuf, idxp->ksiz);
      }
      ln = tclistnum(newnode->idxs);
      for(int i = 0; i < ln; i++){
        idxp = (BDBIDX *)tclistpop(idxs, &rsiz);
        free(idxp->kbuf);
        free(idxp);
      }
      node->dirty = true;
    }
  }
  if(!bdb->tran && !tcbdbcacheadjust(bdb)) return false;
  return true;
}


/* Remove a record of a B+ tree database object.
   `hdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is true, else, it is false. */
static bool tcbdboutimpl(TCBDB *bdb, const char *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  BDBLEAF *leaf = NULL;
  if(bdb->hleaf < 1 || !(leaf = tcbdbgethistleaf(bdb, kbuf, ksiz))){
    uint64_t pid = tcbdbsearchleaf(bdb, kbuf, ksiz);
    if(pid < 1) return NULL;
    if(!(leaf = tcbdbleafload(bdb, pid))) return NULL;
  }
  int ri;
  BDBREC *recp = tcbdbsearchrec(bdb, leaf, kbuf, ksiz, &ri);
  if(!recp){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return NULL;
  }
  if(recp->rest){
    free(recp->vbuf);
    recp->vbuf = tclistshift(recp->rest, &(recp->vsiz));
    if(tclistnum(recp->rest) < 1){
      tclistdel(recp->rest);
      recp->rest = NULL;
    }
  } else {
    free(recp->vbuf);
    free(recp->kbuf);
    int rsiz;
    free(tclistremove(leaf->recs, ri, &rsiz));
  }
  leaf->dirty = true;
  bdb->rnum--;
  if(!bdb->tran && !tcbdbcacheadjust(bdb)) return NULL;
  return true;
}


/* Remove records of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is true, else, it is false. */
static bool tcbdboutlist(TCBDB *bdb, const char *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  BDBLEAF *leaf = NULL;
  if(bdb->hleaf < 1 || !(leaf = tcbdbgethistleaf(bdb, kbuf, ksiz))){
    uint64_t pid = tcbdbsearchleaf(bdb, kbuf, ksiz);
    if(pid < 1) return NULL;
    if(!(leaf = tcbdbleafload(bdb, pid))) return NULL;
  }
  int ri;
  BDBREC *recp = tcbdbsearchrec(bdb, leaf, kbuf, ksiz, &ri);
  if(!recp){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return NULL;
  }
  int rnum = 1;
  if(recp->rest){
    rnum += tclistnum(recp->rest);
    tclistdel(recp->rest);
  }
  free(recp->vbuf);
  free(recp->kbuf);
  int rsiz;
  free(tclistremove(leaf->recs, ri, &rsiz));
  leaf->dirty = true;
  bdb->rnum -= rnum;
  if(!bdb->tran && !tcbdbcacheadjust(bdb)) return NULL;
  return true;
}


/* Retrieve a record in a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `sp' specifies the pointer to the variable into which the size of the region of the return
   value is assigned.
   If successful, the return value is the pointer to the region of the value of the corresponding
   record. */
static const char *tcbdbgetimpl(TCBDB *bdb, const char *kbuf, int ksiz, int *sp){
  assert(bdb && kbuf && ksiz >= 0 && sp);
  BDBLEAF *leaf = NULL;
  if(bdb->hleaf < 1 || !(leaf = tcbdbgethistleaf(bdb, kbuf, ksiz))){
    uint64_t pid = tcbdbsearchleaf(bdb, kbuf, ksiz);
    if(pid < 1) return NULL;
    if(!(leaf = tcbdbleafload(bdb, pid))) return NULL;
  }
  BDBREC *recp = tcbdbsearchrec(bdb, leaf, kbuf, ksiz, NULL);
  if(!recp){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return NULL;
  }
  if(!bdb->tran && !tcbdbcacheadjust(bdb)) return NULL;
  *sp = recp->vsiz;
  return recp->vbuf;
}


/* Get the number of records corresponding a key in a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is the number of the corresponding records, else, it is 0. */
static int tcbdbgetnum(TCBDB *bdb, const char *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  BDBLEAF *leaf = NULL;
  if(bdb->hleaf < 1 || !(leaf = tcbdbgethistleaf(bdb, kbuf, ksiz))){
    uint64_t pid = tcbdbsearchleaf(bdb, kbuf, ksiz);
    if(pid < 1) return 0;
    if(!(leaf = tcbdbleafload(bdb, pid))) return 0;
  }
  BDBREC *recp = tcbdbsearchrec(bdb, leaf, kbuf, ksiz, NULL);
  if(!recp){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return 0;
  }
  return recp->rest ? tclistnum(recp->rest) + 1 : 1;
}


/* Retrieve records in a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is a list object of the values of the corresponding records. */
static TCLIST *tcbdbgetlist(TCBDB *bdb, const char *kbuf, int ksiz){
  assert(bdb && kbuf && ksiz >= 0);
  BDBLEAF *leaf = NULL;
  if(bdb->hleaf < 1 || !(leaf = tcbdbgethistleaf(bdb, kbuf, ksiz))){
    uint64_t pid = tcbdbsearchleaf(bdb, kbuf, ksiz);
    if(pid < 1) return NULL;
    if(!(leaf = tcbdbleafload(bdb, pid))) return NULL;
  }
  BDBREC *recp = tcbdbsearchrec(bdb, leaf, kbuf, ksiz, NULL);
  if(!recp){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return NULL;
  }
  TCLIST *vals;
  TCLIST *rest = recp->rest;
  if(rest){
    int ln = tclistnum(rest);
    vals = tclistnew2(ln + 1);
    tclistpush(vals, recp->vbuf, recp->vsiz);
    for(int i = 0; i < ln; i++){
      int vsiz;
      const char *vbuf = tclistval(rest, i, &vsiz);
      tclistpush(vals, vbuf, vsiz);
    }
  } else {
    vals = tclistnew2(1);
    tclistpush(vals, recp->vbuf, recp->vsiz);
  }
  if(!bdb->tran && !tcbdbcacheadjust(bdb)) return NULL;
  return vals;
}


/* Optimize the file of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `lmemb' specifies the number of members in each leaf page.
   `nmemb' specifies the number of members in each non-leaf page.
   `bnum' specifies the number of elements of the bucket array.
   `apow' specifies the size of record alignment by power of 2.
   `fpow' specifies the maximum number of elements of the free block pool by power of 2.
   `opts' specifies options by bitwise or.
   If successful, the return value is true, else, it is false. */
static bool tcbdboptimizeimpl(TCBDB *bdb, int32_t lmemb, int32_t nmemb,
                              int64_t bnum, int8_t apow, int8_t fpow, uint8_t opts){
  assert(bdb);
  if(lmemb < 1) lmemb = bdb->lmemb;
  if(nmemb < 1) nmemb = bdb->nmemb;
  if(bnum < 1) bnum = tchdbbnum(bdb->hdb);
  if(opts == UINT8_MAX) opts = bdb->opts;
  const char *path = tchdbpath(bdb->hdb);
  char *tpath = tcsprintf("%s%ctmp%c%llu", path, MYEXTCHR, MYEXTCHR, tchdbinode(bdb->hdb));
  TCBDB *tbdb = tcbdbnew();
  tcbdbtune(tbdb, lmemb, nmemb, bnum, apow, fpow, opts);
  if(!tcbdbopen(tbdb, tpath, BDBOWRITER | BDBOCREAT | BDBOTRUNC)){
    tchdbsetecode(bdb->hdb, tcbdbecode(tbdb), __FILE__, __LINE__, __func__);
    tcbdbdel(tbdb);
    free(tpath);
    return false;
  }
  bool err = false;
  BDBCUR *cur = tcbdbcurnew(bdb);
  tcbdbcurfirstimpl(cur);
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  while(!err && cur->id > 0 && tcbdbcurrecimpl(cur, &kbuf, &ksiz, &vbuf, &vsiz)){
    if(!tcbdbputdup(tbdb, kbuf, ksiz, vbuf, vsiz)){
      tchdbsetecode(bdb->hdb, tcbdbecode(tbdb), __FILE__, __LINE__, __func__);
      err = true;
    }
    tcbdbcurnextimpl(cur);
  }
  tcbdbcurdel(cur);
  if(!tcbdbclose(tbdb)){
    tchdbsetecode(bdb->hdb, tcbdbecode(tbdb), __FILE__, __LINE__, __func__);
    err = true;
  }
  tcbdbdel(tbdb);
  if(unlink(path) == -1){
    tchdbsetecode(bdb->hdb, TCEUNLINK, __FILE__, __LINE__, __func__);
    err = true;
  }
  if(rename(tpath, path) == -1){
    tchdbsetecode(bdb->hdb, TCERENAME, __FILE__, __LINE__, __func__);
    err = true;
  }
  free(tpath);
  if(err) return false;
  tpath = tcstrdup(path);
  int omode = (tchdbomode(bdb->hdb) & ~BDBOCREAT) & ~BDBOTRUNC;
  if(!tcbdbcloseimpl(bdb)){
    free(tpath);
    return false;
  }
  bool rv = tcbdbopenimpl(bdb, tpath, omode);
  free(tpath);
  return rv;
}


/* Lock a method of the B+ tree database object.
   `hdb' specifies the B+ tree database object.
   `wr' specifies whether the lock is writer or not.
   If successful, the return value is true, else, it is false. */
static bool tcbdblockmethod(TCBDB *bdb, bool wr){
  assert(bdb);
  if(!bdb->mmtx) return true;
  if(wr ? pthread_rwlock_wrlock(bdb->mmtx) != 0 : pthread_rwlock_wrlock(bdb->mmtx) != 0){
    tchdbsetecode(bdb->hdb, TCETHREAD, __FILE__, __LINE__, __func__);
    return false;
  }
  return true;
}


/* Unlock a method of the B+ tree database object.
   `hdb' specifies the B+ tree database object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbunlockmethod(TCBDB *bdb){
  assert(bdb);
  if(!bdb->mmtx) return true;
  if(pthread_rwlock_unlock(bdb->mmtx) != 0){
    tchdbsetecode(bdb->hdb, TCETHREAD, __FILE__, __LINE__, __func__);
    return false;
  }
  return true;
}


/* Lock the cache of the B+ tree database object.
   `hdb' specifies the B+ tree database object.
   If successful, the return value is true, else, it is false. */
static bool tcbdblockcache(TCBDB *bdb){
  assert(bdb);
  if(!bdb->mmtx) return true;
  if(pthread_mutex_lock(bdb->cmtx) != 0){
    tchdbsetecode(bdb->hdb, TCETHREAD, __FILE__, __LINE__, __func__);
    return false;
  }
  return true;
}


/* Unlock the cache of the B+ tree database object.
   `hdb' specifies the B+ tree database object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbunlockcache(TCBDB *bdb){
  assert(bdb);
  if(!bdb->mmtx) return true;
  if(pthread_mutex_unlock(bdb->cmtx) != 0){
    tchdbsetecode(bdb->hdb, TCETHREAD, __FILE__, __LINE__, __func__);
    return false;
  }
  return true;
}


/* Lock the transaction of the B+ tree database object.
   `hdb' specifies the B+ tree database object.
   If successful, the return value is true, else, it is false. */
static bool tcbdblocktran(TCBDB *bdb){
  assert(bdb);
  if(!bdb->mmtx) return true;
  if(pthread_mutex_lock(bdb->tmtx) != 0){
    tchdbsetecode(bdb->hdb, TCETHREAD, __FILE__, __LINE__, __func__);
    return false;
  }
  return true;
}


/* Unlock the transaction of the B+ tree database object.
   `hdb' specifies the B+ tree database object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbunlocktran(TCBDB *bdb){
  assert(bdb);
  if(!bdb->mmtx) return true;
  if(pthread_mutex_unlock(bdb->tmtx) != 0){
    tchdbsetecode(bdb->hdb, TCETHREAD, __FILE__, __LINE__, __func__);
    return false;
  }
  return true;
}


/* Move a cursor object to the first record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcurfirstimpl(BDBCUR *cur){
  assert(cur);
  cur->id = cur->bdb->first;
  cur->kidx = 0;
  cur->vidx = 0;
  return tcbdbcuradjust(cur, true);
}


/* Move a cursor object to the last record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcurlastimpl(BDBCUR *cur){
  assert(cur);
  cur->id = cur->bdb->last;
  cur->kidx = INT_MAX;
  cur->vidx = INT_MAX;
  return tcbdbcuradjust(cur, false);
}


/* Move a cursor object to around records corresponding a key.
   `cur' specifies the cursor object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `forward' specifies whether the cursor is to be the front of records.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcurjumpimpl(BDBCUR *cur, const char *kbuf, int ksiz, bool forward){
  assert(cur && kbuf && ksiz >= 0);
  TCBDB *bdb = cur->bdb;
  uint64_t pid = tcbdbsearchleaf(bdb, kbuf, ksiz);
  if(pid < 1){
    cur->id = 0;
    cur->kidx = 0;
    cur->vidx = 0;
    return false;
  }
  BDBLEAF *leaf = tcbdbleafload(bdb, pid);
  if(!leaf){
    cur->id = 0;
    cur->kidx = 0;
    cur->vidx = 0;
    return NULL;
  }
  if(tclistnum(leaf->recs) < 1){
    cur->id = pid;
    cur->kidx = 0;
    cur->vidx = 0;
    return forward ? tcbdbcurnextimpl(cur) : tcbdbcurprevimpl(cur);
  }
  int ri;
  BDBREC *recp = tcbdbsearchrec(bdb, leaf, kbuf, ksiz, &ri);
  if(recp){
    cur->id = pid;
    cur->kidx = ri;
    if(forward){
      cur->vidx = 0;
    } else {
      cur->vidx = recp->rest ? tclistnum(recp->rest) : 0;
    }
    return true;
  }
  cur->id = leaf->id;
  if(ri > 0 && ri >= tclistnum(leaf->recs)) ri = tclistnum(leaf->recs) - 1;
  cur->kidx = ri;
  int rsiz;
  recp = (BDBREC *)tclistval(leaf->recs, ri, &rsiz);
  if(forward){
    if(bdb->cmp(kbuf, ksiz, recp->kbuf, recp->ksiz, bdb->cmpop) < 0) return true;
    cur->vidx = recp->rest ? tclistnum(recp->rest) : 0;
    return tcbdbcurnextimpl(cur);
  }
  if(bdb->cmp(kbuf, ksiz, recp->kbuf, recp->ksiz, bdb->cmpop) > 0) return true;
  cur->vidx = 0;
  return tcbdbcurprevimpl(cur);
}


/* Adjust a cursor object forward to the suitable record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcuradjust(BDBCUR *cur, bool forward){
  assert(cur);
  TCBDB *bdb = cur->bdb;
  while(true){
    if(cur->id < 1){
      tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
      cur->id = 0;
      cur->kidx = 0;
      cur->vidx = 0;
      return false;
    }
    BDBLEAF *leaf = tcbdbleafload(bdb, cur->id);
    if(!leaf) return false;
    TCLIST *recs = leaf->recs;
    int knum = tclistnum(recs);
    if(cur->kidx < 0){
      if(forward){
        cur->kidx = 0;
        cur->vidx = 0;
      } else {
        cur->id = leaf->prev;
        cur->kidx = INT_MAX;
        cur->vidx = INT_MAX;
      }
    } else if(cur->kidx >= knum){
      if(forward){
        cur->id = leaf->next;
        cur->kidx = 0;
        cur->vidx = 0;
      } else {
        cur->kidx = knum - 1;
        cur->vidx = INT_MAX;
      }
    } else {
      int rsiz;
      BDBREC *recp = (BDBREC *)tclistval(recs, cur->kidx, &rsiz);
      int vnum = recp->rest ? tclistnum(recp->rest) + 1 : 1;
      if(cur->vidx < 0){
        if(forward){
          cur->vidx = 0;
        } else {
          cur->kidx--;
          cur->vidx = INT_MAX;
        }
      } else if(cur->vidx >= vnum){
        if(forward){
          cur->kidx++;
          cur->vidx = 0;
          if(cur->kidx >= knum){
            cur->id = leaf->next;
            cur->kidx = 0;
            cur->vidx = 0;
          } else {
            break;
          }
        } else {
          cur->vidx = vnum - 1;
          if(cur->vidx >= 0) break;
        }
      } else {
        break;
      }
    }
  }
  return true;
}


/* Move a cursor object to the previous record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcurprevimpl(BDBCUR *cur){
  assert(cur);
  cur->vidx--;
  return tcbdbcuradjust(cur, false);
}


/* Move a cursor object to the next record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcurnextimpl(BDBCUR *cur){
  assert(cur);
  cur->vidx++;
  return tcbdbcuradjust(cur, true);
}


/* Insert a record around a cursor object.
   `cur' specifies the cursor object.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   `cpmode' specifies detail adjustment.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcurputimpl(BDBCUR *cur, const char *vbuf, int vsiz, int cpmode){
  assert(cur && vbuf && vsiz >= 0);
  TCBDB *bdb = cur->bdb;
  BDBLEAF *leaf = tcbdbleafload(bdb, cur->id);
  if(!leaf) return false;
  TCLIST *recs = leaf->recs;
  if(cur->kidx >= tclistnum(recs)){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return false;
  }
  int rsiz;
  BDBREC *recp = (BDBREC *)tclistval(recs, cur->kidx, &rsiz);
  int vnum = recp->rest ? tclistnum(recp->rest) + 1 : 1;
  if(cur->vidx >= vnum){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return false;
  }
  switch(cpmode){
  case BDBCPCURRENT:
    if(cur->vidx < 1){
      if(vsiz > recp->vsiz) recp->vbuf = tcrealloc(recp->vbuf, vsiz + 1);
      memcpy(recp->vbuf, vbuf, vsiz);
      recp->vbuf[vsiz] = '\0';
      recp->vsiz = vsiz;
    } else {
      tclistover(recp->rest, cur->vidx - 1, vbuf, vsiz);
    }
    break;
  case BDBCPBEFORE:
    if(cur->vidx < 1){
      if(!recp->rest) recp->rest = tclistnew();
      tclistunshift(recp->rest, recp->vbuf, recp->vsiz);
      if(vsiz > recp->vsiz) recp->vbuf = tcrealloc(recp->vbuf, vsiz + 1);
      memcpy(recp->vbuf, vbuf, vsiz);
      recp->vbuf[vsiz] = '\0';
      recp->vsiz = vsiz;
    } else {
      tclistinsert(recp->rest, cur->vidx - 1, vbuf, vsiz);
    }
    bdb->rnum++;
    break;
  case BDBCPAFTER:
    if(!recp->rest) recp->rest = tclistnew();
    tclistinsert(recp->rest, cur->vidx, vbuf, vsiz);
    cur->vidx++;
    bdb->rnum++;
    break;
  }
  leaf->dirty = true;
  return true;
}


/* Delete the record where a cursor object is.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false. */
static bool tcbdbcuroutimpl(BDBCUR *cur){
  assert(cur);
  TCBDB *bdb = cur->bdb;
  BDBLEAF *leaf = tcbdbleafload(bdb, cur->id);
  if(!leaf) return false;
  TCLIST *recs = leaf->recs;
  if(cur->kidx >= tclistnum(recs)){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return false;
  }
  int rsiz;
  BDBREC *recp = (BDBREC *)tclistval(recs, cur->kidx, &rsiz);
  int vnum = recp->rest ? tclistnum(recp->rest) + 1 : 1;
  if(cur->vidx >= vnum){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return false;
  }
  if(recp->rest){
    free(recp->vbuf);
    recp->vbuf = tclistshift(recp->rest, &(recp->vsiz));
    if(tclistnum(recp->rest) < 1){
      tclistdel(recp->rest);
      recp->rest = NULL;
    }
  } else {
    free(recp->vbuf);
    free(recp->kbuf);
    int rsiz;
    free(tclistremove(leaf->recs, cur->kidx, &rsiz));
  }
  bdb->rnum--;
  leaf->dirty = true;
  return tcbdbcuradjust(cur, true);
}


/* Get the key and the value of the current record of the cursor object.
   `cur' specifies the cursor object.
   `kbp' specifies the pointer to the variable into which the pointer to the region of the key is
   assgined.
   `ksp' specifies the pointer to the variable into which the size of the key region is assigned.
   `vbp' specifies the pointer to the variable into which the pointer to the region of the value
   is assgined.
   `vsp' specifies the pointer to the variable into which the size of the value region is
   assigned. */
static bool tcbdbcurrecimpl(BDBCUR *cur, const char **kbp, int *ksp, const char **vbp, int *vsp){
  assert(cur && kbp && ksp && vbp && vsp);
  TCBDB *bdb = cur->bdb;
  BDBLEAF *leaf = tcbdbleafload(bdb, cur->id);
  if(!leaf) return false;
  TCLIST *recs = leaf->recs;
  if(cur->kidx >= tclistnum(recs)){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return false;
  }
  int rsiz;
  BDBREC *recp = (BDBREC *)tclistval(recs, cur->kidx, &rsiz);
  int vnum = recp->rest ? tclistnum(recp->rest) + 1 : 1;
  if(cur->vidx >= vnum){
    tchdbsetecode(bdb->hdb, TCENOREC, __FILE__, __LINE__, __func__);
    return false;
  }
  *kbp = recp->kbuf;
  *ksp = recp->ksiz;
  if(cur->vidx > 0){
    *vbp = tclistval(recp->rest, cur->vidx - 1, vsp);
  } else {
    *vbp = recp->vbuf;
    *vsp = recp->vsiz;
  }
  return true;
}



/*************************************************************************************************
 * debugging functions
 *************************************************************************************************/


/* Print meta data of the header into the debugging output.
   `bdb' specifies the B+ tree database object. */
void tcbdbprintmeta(TCBDB *bdb){
  assert(bdb);
  int dbgfd = tchdbdbgfd(bdb->hdb);
  if(dbgfd < 0) return;
  char buf[BDBPAGEBUFSIZ];
  char *wp = buf;
  wp += sprintf(wp, "META:");
  wp += sprintf(wp, " mmtx=%p", (void *)bdb->mmtx);
  wp += sprintf(wp, " cmtx=%p", (void *)bdb->cmtx);
  wp += sprintf(wp, " hdb=%p", (void *)bdb->hdb);
  wp += sprintf(wp, " opaque=%p", (void *)bdb->opaque);
  wp += sprintf(wp, " open=%d", bdb->open);
  wp += sprintf(wp, " wmode=%d", bdb->wmode);
  wp += sprintf(wp, " lmemb=%u", bdb->lmemb);
  wp += sprintf(wp, " nmemb=%u", bdb->nmemb);
  wp += sprintf(wp, " opts=%u", bdb->opts);
  wp += sprintf(wp, " root=%llu", (unsigned long long)bdb->root);
  wp += sprintf(wp, " first=%llu", (unsigned long long)bdb->first);
  wp += sprintf(wp, " last=%llu", (unsigned long long)bdb->last);
  wp += sprintf(wp, " lnum=%llu", (unsigned long long)bdb->lnum);
  wp += sprintf(wp, " nnum=%llu", (unsigned long long)bdb->nnum);
  wp += sprintf(wp, " rnum=%llu", (unsigned long long)bdb->rnum);
  wp += sprintf(wp, " leafc=%p", (void *)bdb->leafc);
  wp += sprintf(wp, " nodec=%p", (void *)bdb->nodec);
  wp += sprintf(wp, " cmp=%p", (void *)(intptr_t)bdb->cmp);
  wp += sprintf(wp, " cmpop=%p", (void *)bdb->cmpop);
  wp += sprintf(wp, " lcnum=%u", bdb->lcnum);
  wp += sprintf(wp, " ncnum=%u", bdb->ncnum);
  wp += sprintf(wp, " hist=%p", (void *)bdb->hist);
  wp += sprintf(wp, " hnum=%d", bdb->hnum);
  wp += sprintf(wp, " hleaf=%llu", (unsigned long long)bdb->hleaf);
  wp += sprintf(wp, " lleaf=%llu", (unsigned long long)bdb->lleaf);
  wp += sprintf(wp, " cnt_saveleaf=%lld", (long long)bdb->cnt_saveleaf);
  wp += sprintf(wp, " cnt_loadleaf=%lld", (long long)bdb->cnt_loadleaf);
  wp += sprintf(wp, " cnt_adjleafc=%lld", (long long)bdb->cnt_adjleafc);
  wp += sprintf(wp, " cnt_savenode=%lld", (long long)bdb->cnt_savenode);
  wp += sprintf(wp, " cnt_loadnode=%lld", (long long)bdb->cnt_loadnode);
  wp += sprintf(wp, " cnt_adjnodec=%lld", (long long)bdb->cnt_adjnodec);
  *(wp++) = '\n';
  tcwrite(dbgfd, buf, wp - buf);
}


/* Print records of a leaf object into the debugging output.
   `bdb' specifies the B+ tree database object.
   `leaf' specifies the leaf object. */
void tcbdbprintleaf(TCBDB *bdb, BDBLEAF *leaf){
  assert(bdb && leaf);
  int dbgfd = tchdbdbgfd(bdb->hdb);
  TCLIST *recs = leaf->recs;
  if(dbgfd < 0) return;
  char buf[BDBPAGEBUFSIZ];
  char *wp = buf;
  wp += sprintf(wp, "LEAF:");
  wp += sprintf(wp, " id:%llx", leaf->id);
  wp += sprintf(wp, " prev:%llx", leaf->prev);
  wp += sprintf(wp, " next:%llx", leaf->next);
  wp += sprintf(wp, " dirty:%d", leaf->dirty);
  wp += sprintf(wp, " rnum:%d", tclistnum(recs));
  *(wp++) = ' ';
  for(int i = 0; i < tclistnum(recs); i++){
    tcwrite(dbgfd, buf, wp - buf);
    wp = buf;
    int rsiz;
    BDBREC *recp = (BDBREC *)tclistval(recs, i, &rsiz);
    wp += sprintf(wp, " [%s:%s]", recp->kbuf, recp->vbuf);
    TCLIST *rest = recp->rest;
    if(rest){
      for(int j = 0; j < tclistnum(rest); j++){
        wp += sprintf(wp, ":%s", tclistval2(rest, j));
      }
    }
  }
  *(wp++) = '\n';
  tcwrite(dbgfd, buf, wp - buf);
}


/* Print indexes of a node object into the debugging output.
   `bdb' specifies the B+ tree database object.
   `node' specifies the node object. */
void tcbdbprintnode(TCBDB *bdb, BDBNODE *node){
  assert(bdb && node);
  int dbgfd = tchdbdbgfd(bdb->hdb);
  TCLIST *idxs = node->idxs;
  if(dbgfd < 0) return;
  char buf[BDBPAGEBUFSIZ];
  char *wp = buf;
  wp += sprintf(wp, "NODE:");
  wp += sprintf(wp, " id:%llx", node->id);
  wp += sprintf(wp, " heir:%llx", node->heir);
  wp += sprintf(wp, " dirty:%d", node->dirty);
  wp += sprintf(wp, " rnum:%d", tclistnum(idxs));
  *(wp++) = ' ';
  for(int i = 0; i < tclistnum(idxs); i++){
    tcwrite(dbgfd, buf, wp - buf);
    wp = buf;
    int rsiz;
    BDBIDX *idxp = (BDBIDX *)tclistval(idxs, i, &rsiz);
    wp += sprintf(wp, " [%llx:%s]", idxp->pid, idxp->kbuf);
  }
  *(wp++) = '\n';
  tcwrite(dbgfd, buf, wp - buf);
}



/* END OF FILE */
