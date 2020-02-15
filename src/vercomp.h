#ifndef VERCOMP_H
#define VERCOMP_H
#include <sqlite3.h>

int modvercomp_install(sqlite3 *db);
int sqlite3_modvercomp_init(
    sqlite3 *db,
    char **pzErrMsg,
    const sqlite3_api_routines *pApi
);
#endif /* VERCOMP_H */

