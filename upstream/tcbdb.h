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


#ifndef _TCBDB_H                         // duplication check
#define _TCBDB_H


#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <tcutil.h>
#include <tchdb.h>



/*************************************************************************************************
 * API
 *************************************************************************************************/


/* type of the pointer to a comparison function.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   `op' specifies the pointer to the optional opaque object.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
typedef int (*BDBCMP)(const char *aptr, int asiz, const char *bptr, int bsiz, void *op);

typedef struct {                         // type of structure for a B+ tree database
  void *mmtx;                            // mutex for method
  void *cmtx;                            // mutex for cache
  void *tmtx;                            // mutex for transaction
  TCHDB *hdb;                            // internal database object
  char *opaque;                          // opaque buffer
  bool open;                             // whether the internal database is opened
  bool wmode;                            // whether to be writable
  uint32_t lmemb;                        // number of members in each leaf
  uint32_t nmemb;                        // number of members in each node
  uint8_t opts;                          // options
  uint64_t root;                         // ID number of the root page
  uint64_t first;                        // ID number of the first leaf
  uint64_t last;                         // ID number of the last leaf
  uint64_t lnum;                         // number of leaves
  uint64_t nnum;                         // number of nodes
  uint64_t rnum;                         // number of records
  TCMAP *leafc;                          // cache for leaves
  TCMAP *nodec;                          // cache for nodes
  BDBCMP cmp;                            // pointer to the comparison function
  void *cmpop;                           // opaque object for the comparison function
  uint32_t lcnum;                        // max number of cached leaves
  uint32_t ncnum;                        // max number of cached nodes
  uint64_t *hist;                        // history array of visited nodes
  int hnum;                              // number of element of the history array
  uint64_t hleaf;                        // ID number of the leaf referred by the history
  uint64_t lleaf;                        // ID number of the last visited leaf
  bool tran;                             // whether in the transaction
  char *rbopaque;                        // opaque for rollback
  int64_t cnt_saveleaf;                  // tesing counter for leaf save times
  int64_t cnt_loadleaf;                  // tesing counter for leaf load times
  int64_t cnt_adjleafc;                  // tesing counter for node cache adjust times
  int64_t cnt_savenode;                  // tesing counter for node save times
  int64_t cnt_loadnode;                  // tesing counter for node load times
  int64_t cnt_adjnodec;                  // tesing counter for node cache adjust times
} TCBDB;

enum {                                   // enumeration for additional flags
  BDBFOPEN = HDBFOPEN,                   // whether opened
  BDBFFATAL = HDBFFATAL                  // whetehr with fatal error
};

enum {                                   // enumeration for tuning options
  BDBTLARGE = 1 << 0,                    // use 64-bit bucket array
  BDBTDEFLATE = 1 << 1,                  // compress each page with Deflate
  BDBTTCBS = 1 << 2,                     // compress each page with TCBS
};

enum {                                   // enumeration for open modes
  BDBOREADER = 1 << 0,                   // open as a reader
  BDBOWRITER = 1 << 1,                   // open as a writer
  BDBOCREAT = 1 << 2,                    // writer creating
  BDBOTRUNC = 1 << 3,                    // writer truncating
  BDBONOLCK = 1 << 4,                    // open without locking
  BDBOLCKNB = 1 << 5                     // lock without blocking
};

enum {                                   // enumeration for transaction locking modes
  BDBTLSHAR,                             // shared locking
  BDBTLEXCL                              // exclusive locking
};

typedef struct {                         // type of structure for a B+ tree cursor
  TCBDB *bdb;                            // database object
  uint64_t id;                           // ID number of the leaf
  int32_t kidx;                          // number of the key
  int32_t vidx;                          // number of the value
} BDBCUR;

enum {                                   // enumeration for cursor put mode
  BDBCPCURRENT,                          // current
  BDBCPBEFORE,                           // before
  BDBCPAFTER                             // after
};


/* Get the message string corresponding to an error code.
   `ecode' specifies the error code. */
const char *tcbdberrmsg(int ecode);


/* Create a B+ tree database object.
   The return value is the new B+ tree database object. */
TCBDB *tcbdbnew(void);


/* Delete a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   If the database is not closed, it is closed implicitly.  Note that the deleted object and its
   derivatives can not be used anymore. */
void tcbdbdel(TCBDB *bdb);


/* Get the last happened error code of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the last happened error code. */
int tcbdbecode(TCBDB *bdb);


/* Set mutual exclusion control of a B+ tree database object for threading.
   `bdb' specifies the B+ tree database object which is not opened.
   If successful, the return value is true, else, it is false.
   Note that the mutual exclusion control of the database should be set before the database is
   opened. */
bool tcbdbsetmutex(TCBDB *bdb);


/* Set the custom comparison function of a B+ tree database object.
   `bdb' specifies the B+ tree database object which is not opened.
   `cmp' specifies the pointer to the custom comparison function.
   `cmpop' specifies an arbitrary pointer to be given as a parameter of the comparison function.
   If it is not needed, `NULL' can be specified.
   The default comparison function compares keys of two records by lexical order.  Note that the
   custom comparison function should be set before the database is opened and should be set
   every time the database is being opened. */
bool tcbdbsetcmpfunc(TCBDB *bdb, BDBCMP cmp, void *cmpop);


/* Set the tuning parameters of a B+ tree database object.
   `bdb' specifies the B+ tree database object which is not opened.
   `lmemb' specifies the number of members in each leaf page.  If it is not more than 0, the
   default value is specified.  The default value is 128.
   `nmemb' specifies the number of members in each non-leaf page.  If it is not more than 0, the
   default value is specified.  The default value is 256.
   `bnum' specifies the number of elements of the bucket array.  If it is not more than 0, the
   default value is specified.  The default value is 16381.
   `apow' specifies the size of record alignment by power of 2.  If it is negative, the default
   value is specified.  The default value is 8 standing for 2^8=256.
   `fpow' specifies the maximum number of elements of the free block pool by power of 2.  If it
   is negative, the default value is specified.  The default value is 10 standing for 2^10=1024.
   `opts' specifies options by bitwise or: `BDBTLARGE' specifies that the size of the database
   can be larger than 2GB by using 64-bit bucket array, `BDBTDEFLATE' specifies that each page
   is compressed with Deflate encoding, `BDBTTCBS' specifies that each page is compressed with
   TCBS encoding.
   If successful, the return value is true, else, it is false.
   Note that the tuning parameters of the database should be set before the database is opened. */
bool tcbdbtune(TCBDB *bdb, int32_t lmemb, int32_t nmemb,
               int64_t bnum, int8_t apow, int8_t fpow, uint8_t opts);


/* Set the caching parameters of a B+ tree database object.
   `bdb' specifies the B+ tree database object which is not opened.
   `lcnum' specifies the maximum number of leaf nodes to be cached.  If it is not more than 0,
   the default value is specified.
   `ncnum' specifies the maximum number of non-leaf nodes to be cached.  If it is not more than 0,
   the default value is specified.
   If successful, the return value is true, else, it is false.
   Note that the tuning parameters of the database should be set before the database is opened. */
bool tcbdbsetcache(TCBDB *bdb, int32_t lcnum, int32_t ncnum);


/* Open a database file and connect a B+ tree database object.
   `bdb' specifies the B+ tree database object which is not opened.
   `path' specifies the path of the database file.
   `omode' specifies the connection mode: `BDBOWRITER' as a writer, `BDBOREADER' as a reader.
   If the mode is `BDBOWRITER', the following may be added by bitwise or: `BDBOCREAT', which
   means it creates a new database if not exist, `BDBOTRUNC', which means it creates a new database
   regardless if one exists.  Both of `BDBOREADER' and `BDBOWRITER' can be added to by
   bitwise or: `BDBONOLOCK', which means it opens the database file without file locking, or
   `BDBOLOCKNB', which means locking is performed without blocking.
   If successful, the return value is true, else, it is false. */
bool tcbdbopen(TCBDB *bdb, const char *path, int omode);


/* Close a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   If successful, the return value is true, else, it is false.
   Update of a database is assured to be written when the database is closed.  If a writer opens
   a database but does not close it appropriately, the database will be broken. */
bool tcbdbclose(TCBDB *bdb);


/* Store a record into a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, it is overwritten. */
bool tcbdbput(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz);


/* Store a string record into a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kstr' specifies the string of the key.
   `vstr' specifies the string of the value.
   `over' specifies whether the value of the duplicated record is overwritten or not.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, it is overwritten. */
bool tcbdbput2(TCBDB *bdb, const char *kstr, const char *vstr);


/* Store a new record into a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, this function has no effect. */
bool tcbdbputkeep(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz);


/* Store a new string record into a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kstr' specifies the string of the key.
   `vstr' specifies the string of the value.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, this function has no effect. */
bool tcbdbputkeep2(TCBDB *bdb, const char *kstr, const char *vstr);


/* Concatenate a value at the end of the existing record in a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   If successful, the return value is true, else, it is false.
   If there is no corresponding record, a new record is created. */
bool tcbdbputcat(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz);


/* Concatenate a stirng value at the end of the existing record in a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kstr' specifies the string of the key.
   `vstr' specifies the string of the value.
   If successful, the return value is true, else, it is false.
   If there is no corresponding record, a new record is created. */
bool tcbdbputcat2(TCBDB *bdb, const char *kstr, const char *vstr);


/* Store a new record into a B+ tree database object with allowing duplication of keys.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, the new record is placed after the
   existing one. */
bool tcbdbputdup(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz);


/* Store a new string record into a B+ tree database object with allowing duplication of keys.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kstr' specifies the string of the key.
   `vstr' specifies the string of the value.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, the new record is placed after the
   existing one. */
bool tcbdbputdup2(TCBDB *bdb, const char *kstr, const char *vstr);


/* Remove a record of a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is true, else, it is false.
   If the key of duplicated records is specified, the value of the first record is selected. */
bool tcbdbout(TCBDB *bdb, const void *kbuf, int ksiz);


/* Remove a string record of a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kstr' specifies the string of the key.
   If successful, the return value is true, else, it is false.
   If the key of duplicated records is specified, the value of the first record is selected. */
bool tcbdbout2(TCBDB *bdb, const char *kstr);


/* Remove records of a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is true, else, it is false.
   If the key of duplicated records is specified, all of them are removed. */
bool tcbdbout3(TCBDB *bdb, const void *kbuf, int ksiz);


/* Retrieve a record in a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `sp' specifies the pointer to the variable into which the size of the region of the return
   value is assigned.
   If successful, the return value is the pointer to the region of the value of the corresponding
   record.  `NULL' is returned if no record corresponds.
   If the key of duplicated records is specified, the value of the first record is selected.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call when
   it is no longer in use. */
void *tcbdbget(TCBDB *bdb, const void *kbuf, int ksiz, int *sp);


/* Retrieve a string record in a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kstr' specifies the string of the key.
   If successful, the return value is the string of the value of the corresponding record.
   `NULL' is returned if no record corresponds.
   If the key of duplicated records is specified, the value of the first record is selected.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call when it is no longer in use. */
char *tcbdbget2(TCBDB *bdb, const char *kstr);


/* Retrieve a record in a B+ tree database object as a volatile buffer.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `sp' specifies the pointer to the variable into which the size of the region of the return
   value is assigned.
   If successful, the return value is the pointer to the region of the value of the corresponding
   record.  `NULL' is returned if no record corresponds.
   If the key of duplicated records is specified, the value of the first record is selected.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return value
   is volatile and it may be spoiled by another operation of the database, the data should be
   copied into another involatile buffer immediately. */
const void *tcbdbget3(TCBDB *bdb, const void *kbuf, int ksiz, int *sp);


/* Retrieve records in a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is a list object of the values of the corresponding records.
   `NULL' is returned if no record corresponds.
   Because the object of the return value is created with the function `tclistnew', it should
   be deleted with the function `tclistdel' when it is no longer in use. */
TCLIST *tcbdbget4(TCBDB *bdb, const void *kbuf, int ksiz);


/* Get the number of records corresponding a key in a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is the number of the corresponding records, else, it is 0. */
int tcbdbvnum(TCBDB *bdb, const void *kbuf, int ksiz);


/* Get the size of the value of a record in a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is the size of the value of the corresponding record, else,
   it is -1.
   If the key of duplicated records is specified, the value of the first record is selected. */
int tcbdbvsiz(TCBDB *bdb, const void *kbuf, int ksiz);


/* Synchronize updated contents of a B+ tree database object with the file and the device.
   `bdb' specifies the B+ tree database object connected as a writer.
   If successful, the return value is true, else, it is false.
   This function is useful when another process connects the same database file. */
bool tcbdbsync(TCBDB *bdb);


/* Optimize the file of a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   `lmemb' specifies the number of members in each leaf page.  If it is not more than 0, the
   current setting is not changed.
   `nmemb' specifies the number of members in each non-leaf page.  If it is not more than 0, the
   current setting is not changed.
   `bnum' specifies the number of elements of the bucket array.  If it is not more than 0, the
   current setting is not changed.
   `apow' specifies the size of record alignment by power of 2.  If it is negative, the current
   setting is not changed.
   `fpow' specifies the maximum number of elements of the free block pool by power of 2.  If it
   is negative, the current setting is not changed.
   `opts' specifies options by bitwise or: `BDBTLARGE' specifies that the size of the database
   can be larger than 2GB by using 64-bit bucket array, `BDBTDEFLATE' specifies that each record
   is compressed with Deflate encoding, `BDBTTCBS' specifies that each page is compressed with
   TCBS encoding.  If it is `UINT8_MAX', the default setting is not changed.
   If successful, the return value is true, else, it is false.
   This function is useful to reduce the size of the database file with data fragmentation by
   successive updating. */
bool tcbdboptimize(TCBDB *bdb, int32_t lmemb, int32_t nmemb,
                   int64_t bnum, int8_t apow, int8_t fpow, uint8_t opts);


/* Begin the transaction of a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   The database is locked by the thread while the transaction so that only one transaction can be
   activated with a database object at the same time.  Thus, the serializable isolation level is
   assumed if every database operation is performed in the transaction. */
bool tcbdbtranbegin(TCBDB *bdb);


/* Commit the transaction of a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   If successful, the return value is true, else, it is false.
   Update in the transaction is fixed when it is committed successfully. */
bool tcbdbtrancommit(TCBDB *bdb);


/* Abort the transaction of a B+ tree database object.
   `bdb' specifies the B+ tree database object connected as a writer.
   If successful, the return value is true, else, it is false.
   Update in the transaction is discarded when it is aborted.  The state of the database is
   rollbacked to before transaction. */
bool tcbdbtranabort(TCBDB *bdb);


/* Get the file path of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the path of the database file or `NULL' if the object does not connect to
   any database file. */
const char *tcbdbpath(TCBDB *bdb);


/* Get the number of records of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the number of records or 0 if the object does not connect to any database
   file. */
uint64_t tcbdbrnum(TCBDB *bdb);


/* Get the size of the database file of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the size of the database file or 0 if the object does not connect to any
   database file. */
uint64_t tcbdbfsiz(TCBDB *bdb);


/* Create a cursor object.
   `bdb' specifies the B+ tree database object.
   The return value is the new cursor object.
   Note that the cursor is available only after initialization with the `tcbdbcurfirst' or the
   `tcbdbcurjump' functions and so on.  Moreover, the position of the cursor will be indefinite
   when the database is updated after the initialization of the cursor. */
BDBCUR *tcbdbcurnew(TCBDB *bdb);


/* Delete a cursor object.
   `cur' specifies the cursor object. */
void tcbdbcurdel(BDBCUR *cur);


/* Move a cursor object to the first record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false.  False is returned if there is
   no record in the database. */
bool tcbdbcurfirst(BDBCUR *cur);


/* Move a cursor object to the last record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false.  False is returned if there is
   no record in the database. */
bool tcbdbcurlast(BDBCUR *cur);


/* Move a cursor object to the front of records corresponding a key.
   `cur' specifies the cursor object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is true, else, it is false.  False is returned if there is
   no record corresponding the condition.
   The cursor is set to the first record corresponding the key or the next substitute if
   completely matching record does not exist. */
bool tcbdbcurjump(BDBCUR *cur, const char *kbuf, int ksiz);


/* Move a cursor object to the previous record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false.  False is returned if there is
   no previous record. */
bool tcbdbcurprev(BDBCUR *cur);


/* Move a cursor object to the next record.
   `cur' specifies the cursor object.
   If successful, the return value is true, else, it is false.  False is returned if there is
   no next record. */
bool tcbdbcurnext(BDBCUR *cur);


/* Insert a record around a cursor object.
   `cur' specifies the cursor object of writer connection.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   `cpmode' specifies detail adjustment: `BDBCPCURRENT', which means that the value of the
   current record is overwritten, `BDBCPBEFORE', which means that the new record is inserted
   before the current record, `BDBCPAFTER', which means that the new record is inserted after the
   current record.
   If successful, the return value is true, else, it is false.  False is returned when the cursor
   is at invalid position.
   After insertion, the cursor is moved to the inserted record. */
bool tcbdbcurput(BDBCUR *cur, const char *vbuf, int vsiz, int cpmode);


/* Insert a string record around a cursor object.
   `cur' specifies the cursor object of writer connection.
   `vstr' specifies the string of the value.
   `cpmode' specifies detail adjustment: `BDBCPCURRENT', which means that the value of the
   current record is overwritten, `BDBCPBEFORE', which means that the new record is inserted
   before the current record, `BDBCPAFTER', which means that the new record is inserted after the
   current record.
   If successful, the return value is true, else, it is false.  False is returned when the cursor
   is at invalid position.
   After insertion, the cursor is moved to the inserted record. */
bool tcbdbcurput2(BDBCUR *cur, const char *vstr, int cpmode);


/* Delete the record where a cursor object is.
   `cur' specifies the cursor object of writer connection.
   If successful, the return value is true, else, it is false.  False is returned when the cursor
   is at invalid position.
   After deletion, the cursor is moved to the next record if possible. */
bool tcbdbcurout(BDBCUR *cur);


/* Get the key of the record where the cursor object is.
   `cur' specifies the cursor object.
   `sp' specifies the pointer to the variable into which the size of the region of the return
   value is assigned.
   If successful, the return value is the pointer to the region of the key, else, it is `NULL'.
   `NULL' is returned when the cursor is at invalid position.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call when
   it is no longer in use. */
char *tcbdbcurkey(BDBCUR *cur, int *sp);


/* Get the key string of the record where the cursor object is.
   `cur' specifies the cursor object.
   If successful, the return value is the string of the key, else, it is `NULL'.  `NULL' is
   returned when the cursor is at invalid position.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call when it is no longer in use. */
char *tcbdbcurkey2(BDBCUR *cur);


/* Get the key of the record where the cursor object is, as a volatile buffer.
   `cur' specifies the cursor object.
   `sp' specifies the pointer to the variable into which the size of the region of the return
   value is assigned.
   If successful, the return value is the pointer to the region of the key, else, it is `NULL'.
   `NULL' is returned when the cursor is at invalid position.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return value
   is volatile and it may be spoiled by another operation of the database, the data should be
   copied into another involatile buffer immediately. */
const char *tcbdbcurkey3(BDBCUR *cur, int *sp);


/* Get the value of the record where the cursor object is.
   `cur' specifies the cursor object.
   `sp' specifies the pointer to the variable into which the size of the region of the return
   value is assigned.
   If successful, the return value is the pointer to the region of the value, else, it is `NULL'.
   `NULL' is returned when the cursor is at invalid position.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call when
   it is no longer in use. */
char *tcbdbcurval(BDBCUR *cur, int *sp);


/* Get the value string of the record where the cursor object is.
   `cur' specifies the cursor object.
   If successful, the return value is the string of the value, else, it is `NULL'.  `NULL' is
   returned when the cursor is at invalid position.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call when it is no longer in use. */
char *tcbdbcurval2(BDBCUR *cur);


/* Get the value of the record where the cursor object is, as a volatile buffer.
   `cur' specifies the cursor object.
   `sp' specifies the pointer to the variable into which the size of the region of the return
   value is assigned.
   If successful, the return value is the pointer to the region of the value, else, it is `NULL'.
   `NULL' is returned when the cursor is at invalid position.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return value
   is volatile and it may be spoiled by another operation of the database, the data should be
   copied into another involatile buffer immediately. */
const char *tcbdbcurval3(BDBCUR *cur, int *sp);


/* Get the key and the value of the record where the cursor object is.
   `cur' specifies the cursor object.
   `kxstr' specifies the object into which the key is wrote down.
   `vxstr' specifies the object into which the value is wrote down.
   If successful, the return value is true, else, it is false.  False is returned when the cursor
   is at invalid position. */
bool tcbdbcurrec(BDBCUR *cur, TCXSTR *kxstr, TCXSTR *vxstr);



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Set the file descriptor for debugging output.
   `bdb' specifies the B+ tree database object.
   `fd' specifies the file descriptor for debugging output. */
void tcbdbsetdbgfd(TCBDB *bdb, int fd);


/* Get the file descriptor for debugging output.
   `bdb' specifies the B+ tree database object.
   The return value is the file descriptor for debugging output. */
int tcbdbdbgfd(TCBDB *bdb);


/* Synchronize updating contents on memory.
   `bdb' specifies the B+ tree database object connected as a writer.
   `phys' specifies whether to synchronize physically.
   If successful, the return value is true, else, it is false. */
bool tcbdbmemsync(TCBDB *bdb, bool phys);


/* Get the maximum number of cached leaf nodes of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the maximum number of cached leaf nodes. */
uint32_t tcbdblmemb(TCBDB *bdb);


/* Get the maximum number of cached non-leaf nodes of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the maximum number of cached non-leaf nodes. */
uint32_t tcbdbnmemb(TCBDB *bdb);


/* Get the number of the leaf nodes of B+ tree database object.
   `bdb' specifies the B+ tree database object.
   If successful, the return value is the number of the leaf nodes or 0 if the object does not
   connect to any database file. */
uint64_t tcbdblnum(TCBDB *bdb);


/* Get the number of the non-leaf nodes of B+ tree database object.
   `bdb' specifies the B+ tree database object.
   If successful, the return value is the number of the non-leaf nodes or 0 if the object does
   not connect to any database file. */
uint64_t tcbdbnnum(TCBDB *bdb);


/* Get the number of elements of the bucket array of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the number of elements of the bucket array or 0 if the object does not
   connect to any database file. */
uint64_t tcbdbbnum(TCBDB *bdb);


/* Get the record alignment of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the record alignment or 0 if the object does not connect to any database
   file. */
uint32_t tcbdbalign(TCBDB *bdb);


/* Get the maximum number of the free block pool of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the maximum number of the free block pool or 0 if the object does not
   connect to any database file. */
uint32_t tcbdbfbpmax(TCBDB *bdb);


/* Get the inode number of the database file of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the inode number of the database file or 0 the object does not connect to
   any database file. */
uint64_t tcbdbinode(TCBDB *bdb);


/* Get the modification time of the database file of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the inode number of the database file or 0 the object does not connect to
   any database file. */
time_t tcbdbmtime(TCBDB *bdb);


/* Get the additional flags of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the additional flags. */
uint8_t tcbdbflags(TCBDB *bdb);


/* Get the options of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the options. */
uint8_t tcbdbopts(TCBDB *bdb);


/* Get the pointer to the opaque field of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the pointer to the opaque field whose size is 128 bytes. */
char *tcbdbopaque(TCBDB *bdb);


/* Get the number of used elements of the bucket array of a B+ tree database object.
   `bdb' specifies the B+ tree database object.
   The return value is the number of used elements of the bucket array or 0 if the object does not
   connect to any database file. */
uint64_t tcbdbbnumused(TCBDB *bdb);


/* Store a new record into a B+ tree database object with backward duplication.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of the value.
   `vsiz' specifies the size of the region of the value.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, the new record is placed after the
   existing one. */
bool tcbdbputdupback(TCBDB *bdb, const void *kbuf, int ksiz, const void *vbuf, int vsiz);


/* Store a new string record into a B+ tree database object with backward duplication.
   `bdb' specifies the B+ tree database object connected as a writer.
   `kstr' specifies the string of the key.
   `vstr' specifies the string of the value.
   If successful, the return value is true, else, it is false.
   If a record with the same key exists in the database, the new record is placed after the
   existing one. */
bool tcbdbputdupback2(TCBDB *bdb, const char *kstr, const char *vstr);


/* Move a cursor object to the rear of records corresponding a key.
   `cur' specifies the cursor object.
   `kbuf' specifies the pointer to the region of the key.
   `ksiz' specifies the size of the region of the key.
   If successful, the return value is true, else, it is false.  False is returned if there is
   no record corresponding the condition.
   The cursor is set to the last record corresponding the key or the previous substitute if
   completely matching record does not exist. */
bool tcbdbcurjumpback(BDBCUR *cur, const char *kbuf, int ksiz);


/* Compare two keys by lexical order.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   `op' specifies the pointer to the optional opaque object.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
int tcbdbcmplexical(const char *aptr, int asiz, const char *bptr, int bsiz, void *op);


/* Compare two keys as decimal strings of real numbers.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   `op' is ignored.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
int tcbdbcmpdecimal(const char *aptr, int asiz, const char *bptr, int bsiz, void *op);


/* Compare two keys as 32-bit integers in the native byte order.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   `op' is ignored.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
int tcbdbcmpint32(const char *aptr, int asiz, const char *bptr, int bsiz, void *op);


/* Compare two keys as 64-bit integers in the native byte order.
   `aptr' specifies the pointer to the region of one key.
   `asiz' specifies the size of the region of one key.
   `bptr' specifies the pointer to the region of the other key.
   `bsiz' specifies the size of the region of the other key.
   `op' is ignored.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent. */
int tcbdbcmpint64(const char *aptr, int asiz, const char *bptr, int bsiz, void *op);



#endif                                   // duplication check


// END OF FILE
