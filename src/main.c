/* main.c
 *
 * Copyright 2020 Dingyuan Wang
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "abbs_dep-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <sqlite3.h>

#include "vercomp.h"

#define EXIT_CIRCULAR 2

#define PKG_VISITED 1
#define PKG_BUILDDEP 2
#define PKG_NOT_FOUND 4
#define PKG_DEP_NOT_MET 8

typedef struct {
    int depth;
    int flag;
    char *name;
    char *version;
    GSList *deps;
} depitem;

typedef struct {
    sqlite3 *db;
    sqlite3_stmt *stmt_package;
    sqlite3_stmt *stmt_dep;
} db_ctx;

static void _check_sqlite_ret(int lineno, int ret) {
    if (ret != SQLITE_OK) {
        g_printerr("line %d: sqlite error: %s\n", lineno, sqlite3_errstr(ret));
        exit(EXIT_FAILURE);
    }
}

#define check_sqlite_ret(ret) (_check_sqlite_ret(__LINE__, (ret)))

static const char *sql_package = (
    "SELECT dpkg_version(pv.version, pv.release, pv.epoch) full_version "
    "FROM package_versions pv "
    "INNER JOIN packages p ON p.name=pv.package "
    "INNER JOIN trees t ON t.name=p.tree AND t.mainbranch=pv.branch "
    "WHERE pv.package=?1 "
    "AND (pv.architecture='') != (pv.architecture=?2)"
);
static const char *sql_dep = (
    "SELECT pd.dependency, "
    "  dpkg_version(pv.version, pv.release, pv.epoch) full_version, "
    "  (pd.relationship='BUILDDEP') builddep "
    "FROM package_dependencies pd "
    "LEFT JOIN package_versions pv "
    "ON pv.package=pd.dependency "
    "AND compare_dpkgrel("
    "  dpkg_version(pv.version, pv.release, pv.epoch), "
    "  pd.relop, pd.version) "
    "AND (pv.architecture='') != (pv.architecture=?2) "
    "LEFT JOIN packages p ON p.name=pv.package "
    "LEFT JOIN trees t ON t.name=p.tree "
    "LEFT JOIN dpkg_packages dp ON dp.package=pd.dependency "
    "AND dp.architecture=?2 AND compare_dpkgrel( "
    "  dpkg_version(pv.version, pv.release, pv.epoch), '=', dp.version) "
    "WHERE pd.package=?1 AND pd.dependency!=?1 "
    "AND pd.relationship IN ('PKGDEP', ?3) "
    "AND (pd.architecture='') != (pd.architecture=?2) "
    "AND (pv.package IS NULL OR pv.branch=t.mainbranch) "
    "AND dp.package IS NULL"
);

static db_ctx init_db(char *filename) {
    db_ctx ctx;
    check_sqlite_ret(sqlite3_open_v2(
        filename, &ctx.db, SQLITE_OPEN_READONLY, NULL));
    check_sqlite_ret(sqlite3_busy_timeout(ctx.db, 1000));
    check_sqlite_ret(sqlite3_prepare_v2(
        ctx.db, sql_package, -1, &ctx.stmt_package, NULL));
    check_sqlite_ret(sqlite3_prepare_v2(
        ctx.db, sql_dep, -1, &ctx.stmt_dep, NULL));
    return ctx;
}


/*
 * root: last
 * root->deps: last
 * cur_package: this, optional
 * cur_package->deps: this
 * dep_package: next
 * dep_package->deps: next
 */

void find_deps(
    db_ctx *ctx, char *arch, gboolean builddep, gboolean verbose,
    char *root_package, int depth,
    GSList *packages, GHashTable *dep_table
) {
    GSList *ptr_package = packages;
    char *cur_package_name;
    depitem *cur_item;
    depitem *next_item;
    char *dep_name;
    const char *p_dep_version;
    int ret;
    while (ptr_package != NULL) {
        int depnum;
        cur_package_name = ptr_package->data;
        if (verbose) {
            if (root_package == NULL) {
                g_printerr("%d (root) -> %s\n", depth, cur_package_name);
            } else {
                g_printerr("%d %s -> %s\n", depth, root_package, cur_package_name);
            }
        }
        ptr_package = g_slist_next(ptr_package);
        cur_item = g_hash_table_lookup(dep_table, cur_package_name);
        if (cur_item == NULL) {
            cur_item = calloc(1, sizeof(depitem));
            cur_item->name = cur_package_name;
            g_hash_table_insert(dep_table, cur_package_name, cur_item);
        } else if (cur_item->flag & PKG_VISITED) {
            /* already visited */
            continue;
        }
        cur_item->flag = PKG_VISITED;
        /* first recursion */
        if (cur_item->version == NULL) {
            check_sqlite_ret(sqlite3_bind_text(
                ctx->stmt_package, 1, cur_package_name, -1, NULL));
            check_sqlite_ret(sqlite3_bind_text(
                ctx->stmt_package, 2, arch, -1, NULL));
            ret = sqlite3_step(ctx->stmt_package);
            if (ret == SQLITE_ROW) {
                cur_item->version = g_strdup(
                    (const char *)sqlite3_column_text(ctx->stmt_package, 0));
                check_sqlite_ret(sqlite3_reset(ctx->stmt_package));
            } else if (ret == SQLITE_DONE) {
                cur_item->flag |= PKG_NOT_FOUND;
                check_sqlite_ret(sqlite3_reset(ctx->stmt_package));
                continue;
            } else {
                check_sqlite_ret(ret);
            }
        }

        check_sqlite_ret(sqlite3_bind_text(
            ctx->stmt_dep, 1, cur_package_name, -1, NULL));
        check_sqlite_ret(sqlite3_bind_text(ctx->stmt_dep, 2, arch, -1, NULL));
        if (builddep) {
            ret = sqlite3_bind_text(ctx->stmt_dep, 3, "BUILDDEP", -1, NULL);
        } else {
            ret = sqlite3_bind_text(ctx->stmt_dep, 3, "PKGDEP", -1, NULL);
        }
        check_sqlite_ret(ret);
        for (depnum = 0; ; depnum++) {
            ret = sqlite3_step(ctx->stmt_dep);
            if (ret == SQLITE_ROW) {
                dep_name = g_strdup(
                    (const char *)sqlite3_column_text(ctx->stmt_dep, 0));
                p_dep_version = (const char *)sqlite3_column_text(ctx->stmt_dep, 1);
                cur_item->deps = g_slist_prepend(cur_item->deps, dep_name);
                next_item = g_hash_table_lookup(dep_table, dep_name);
                if (next_item == NULL) {
                    next_item = calloc(1, sizeof(depitem));
                    next_item->name = dep_name;
                    next_item->version = g_strdup(p_dep_version);
                    g_hash_table_insert(dep_table, dep_name, next_item);
                }
                if (sqlite3_column_int(ctx->stmt_dep, 2)) {
                    next_item->flag |= PKG_BUILDDEP;
                }
                if (p_dep_version == NULL) {
                    next_item->flag |= PKG_DEP_NOT_MET;
                }
            } else if (ret == SQLITE_DONE) {
                break;
            } else {
                check_sqlite_ret(ret);
            }
        }
        check_sqlite_ret(sqlite3_reset(ctx->stmt_dep));
        find_deps(ctx, arch, builddep, verbose,
                  cur_package_name, depth + 1, cur_item->deps, dep_table);
    }
    return;
}


int calc_depth(GHashTable *dep_table, char *package, int loop) {
	int max_depth = 0;
    int depth;
    depitem *item;
    GSList *ptr_item;
    char *next_package;
    item = g_hash_table_lookup(dep_table, package);
    if (item == NULL) return 0;
    if ((ptr_item = item->deps) == NULL) {
        item->depth = 1;
        return 1;
    }
	if ((depth = item->depth) < 0) return depth;

    item->depth = loop;
    while (ptr_item != NULL) {
        next_package = ptr_item->data;
        ptr_item = g_slist_next(ptr_item);
        depth = calc_depth(dep_table, next_package, loop);
        if (depth < 0) {
            max_depth = depth;
            break;
        } else if (max_depth < depth + 1) {
            max_depth = depth + 1;
        }
    }
    item->depth = max_depth;
    return max_depth;
}


void toposort(GHashTable *dep_table, GPtrArray *levels, GPtrArray *loops) {
    GHashTableIter iter;
    int loop = -1;
    char *name;
    depitem *item;
    int depth;
    GSList **ptr_arr;
    g_hash_table_iter_init(&iter, dep_table);
    while (g_hash_table_iter_next(&iter, (void **)&name, (void **)&item)) {
        depth = item->depth;
        if (!depth) {
            depth = calc_depth(dep_table, name, loop);
            if (depth == loop) loop--;
        }
    }
    g_hash_table_iter_init(&iter, dep_table);
    while (g_hash_table_iter_next(&iter, (void **)&name, (void **)&item)) {
        depth = item->depth;
        if (depth > 0) {
            if ((guint)depth > levels->len) g_ptr_array_set_size(levels, depth);
            ptr_arr = (GSList **)&g_ptr_array_index(levels, depth - 1);
        } else {
            if ((guint)(-depth) > loops->len) g_ptr_array_set_size(loops, -depth);
            ptr_arr = (GSList **)&g_ptr_array_index(loops, -depth - 1);
        }
        *ptr_arr = g_slist_prepend(*ptr_arr, name);
    }
}


gint main(gint argc, gchar *argv[]) {
    g_autoptr(GOptionContext) context = NULL;
    g_autoptr(GError) error = NULL;
    gboolean version = FALSE;
    gchar *arch = "amd64";
    gboolean verbose = FALSE;
    gboolean builddep = TRUE;
    gchar *dbfile = NULL;
    GOptionEntry main_entries[] = {
        {"version", 0, 0, G_OPTION_ARG_NONE, &version,
            "Show program version", NULL},
        {"arch", 'a', 0, G_OPTION_ARG_STRING, &arch,
            "Set architecture to look up, default 'amd64'", NULL},
        {"no-builddep", 'n', G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &builddep,
            "Don't include BUILDDEP", NULL},
        {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
            "Show progress", NULL},
        {"dbfile", 'd', 0, G_OPTION_ARG_FILENAME, &dbfile,
            "abbs-meta database file", NULL},
        {NULL}
    };

    context = g_option_context_new("package...");
    g_option_context_set_summary(
        context,
        "Resolve dependencies for abbs trees.\n\n"
        "This tool is intended for use with abbs.db database file \n"
        "generated from a `abbs-meta` local scan and `dpkgrepo.py`\n"
        "sync with appropriate sources.list.\n\n"
        "Exit status 2 indicates that there is a dependency loop."
    );
    g_option_context_add_main_entries(context, main_entries, NULL);

    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        return EXIT_FAILURE;
    }
    if (version) {
        g_printerr("%s\n", PACKAGE_VERSION);
        return EXIT_SUCCESS;
    }
    if (dbfile == NULL) {
        g_printerr("error: database file not specified\n");
        fputs(g_option_context_get_help(context, TRUE, NULL), stderr);
        return EXIT_FAILURE;
    }
    if (argc < 2) {
        g_printerr ("error: no package specified\n");
        return EXIT_FAILURE;
    }

    check_sqlite_ret(sqlite3_auto_extension(
        (void (*)(void))sqlite3_modvercomp_init));
    db_ctx ctx = init_db(dbfile);

    GSList *packages = NULL;
    GHashTable *dep_table = g_hash_table_new(g_str_hash, g_str_equal);
    GPtrArray *levels = g_ptr_array_sized_new(10);
    GPtrArray *loops = g_ptr_array_sized_new(2);
    GPtrArray *cur_array = levels;
    GSList *cur_level = NULL;
    // depitem *cur_item;
    char *cur_package_name;
    for (gint i = 1; i < argc; i++) {
        packages = g_slist_prepend(packages, argv[i]);
    };
    find_deps(&ctx, arch, builddep, verbose, NULL, 0, packages, dep_table);
    toposort(dep_table, levels, loops);
    for (guint j = 0; j < 2; j++) {
        if (j) cur_array = loops;
        for (guint i = 0; i < cur_array->len; i++) {
            if (j && !i) puts("=== Dependency loops ===");
            cur_level = g_ptr_array_index(cur_array, i);
            while (cur_level != NULL) {
                cur_package_name = (char *)cur_level->data;
                // cur_item = g_hash_table_lookup(dep_table, cur_package_name);
                // g_print("%s==%s ", cur_package_name, cur_item->version);
                g_print("%s ", cur_package_name);
                cur_level = g_slist_next(cur_level);
            }
            puts("");
        }
    }
    if (loops->len) {return EXIT_CIRCULAR;}
    return EXIT_SUCCESS;
}
