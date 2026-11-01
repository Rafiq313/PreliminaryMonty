#ifndef MONTY_SQLITE_API_H
#define MONTY_SQLITE_API_H
/* Public SQLite C ABI subset. Prefer the installed SDK header when available.
 * Runtime-only Linux installations can link libsqlite3.so.0 without an SDK.
 * Declarations: https://www.sqlite.org/c3ref/intro.html */
#if defined(__has_include)
#if __has_include(<sqlite3.h>)
#include <sqlite3.h>
#define MONTY_HAVE_SQLITE_HEADER 1
#endif
#endif
#ifndef MONTY_HAVE_SQLITE_HEADER
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef long long sqlite3_int64;
int sqlite3_open_v2(const char *,sqlite3 **,int,const char *);
int sqlite3_close(sqlite3 *);
const char *sqlite3_errmsg(sqlite3 *);
int sqlite3_exec(sqlite3 *,const char *,int (*)(void *,int,char **,char **),void *,char **);
int sqlite3_prepare_v2(sqlite3 *,const char *,int,sqlite3_stmt **,const char **);
int sqlite3_step(sqlite3_stmt *);
int sqlite3_finalize(sqlite3_stmt *);
int sqlite3_bind_text(sqlite3_stmt *,int,const char *,int,void (*)(void *));
int sqlite3_bind_blob(sqlite3_stmt *,int,const void *,int,void (*)(void *));
int sqlite3_bind_null(sqlite3_stmt *,int);
int sqlite3_reset(sqlite3_stmt *);
int sqlite3_clear_bindings(sqlite3_stmt *);
sqlite3_int64 sqlite3_last_insert_rowid(sqlite3 *);
int sqlite3_bind_int64(sqlite3_stmt *,int,sqlite3_int64);
int sqlite3_column_int(sqlite3_stmt *,int);
sqlite3_int64 sqlite3_column_int64(sqlite3_stmt *,int);
int sqlite3_column_type(sqlite3_stmt *,int);
const unsigned char *sqlite3_column_text(sqlite3_stmt *,int);
const void *sqlite3_column_blob(sqlite3_stmt *,int);
int sqlite3_column_bytes(sqlite3_stmt *,int);
#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101
#define SQLITE_INTEGER 1
#define SQLITE_TEXT 3
#define SQLITE_BLOB 4
#define SQLITE_NULL 5
#define SQLITE_OPEN_READONLY 1
#define SQLITE_OPEN_READWRITE 2
#define SQLITE_OPEN_CREATE 4
#endif
#endif
