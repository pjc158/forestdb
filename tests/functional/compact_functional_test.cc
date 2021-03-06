/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#if !defined(WIN32) && !defined(_WIN32)
#include <unistd.h>
#endif

#include "libforestdb/forestdb.h"
#include "test.h"

#include "internal_types.h"
#include "functional_util.h"

void compact_wo_reopen_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 3;
    fdb_file_handle *dbfile, *dbfile_new;
    fdb_kvs_handle *db;
    fdb_kvs_handle *db_new;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc;
    fdb_status status;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 16777216;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compact_wo_reopen_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    fdb_open(&dbfile_new, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile_new, &db_new, &kvs_config);
    status = fdb_set_log_callback(db_new, logCallbackFunc,
                                  (void *) "compact_wo_reopen_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // insert documents
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // remove doc
    fdb_doc_create(&rdoc, doc[1]->key, doc[1]->keylen, doc[1]->meta, doc[1]->metalen, NULL, 0);
    rdoc->deleted = true;
    fdb_set(db, rdoc);
    fdb_doc_free(rdoc);

    // commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // perform compaction using one handle
    fdb_compact(dbfile, (char *) "./dummy2");

    // retrieve documents using the other handle without close/re-open
    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db_new, rdoc);

        if (i != 1) {
            TEST_CHK(status == FDB_RESULT_SUCCESS);
            TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
            TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        }else{
            TEST_CHK(status == FDB_RESULT_KEY_NOT_FOUND);
        }

        // free result document
        fdb_doc_free(rdoc);
    }
    // check the other handle's filename
    fdb_file_info info;
    fdb_get_file_info(dbfile_new, &info);
    TEST_CHK(!strcmp("./dummy2", info.filename));

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // close db file
    fdb_kvs_close(db);
    fdb_kvs_close(db_new);
    fdb_close(dbfile);
    fdb_close(dbfile_new);

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("compaction without reopen test");
}

void compact_with_reopen_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 100;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc;
    fdb_status status;

    char keybuf[256], metabuf[256], bodybuf[256], temp[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 16777216;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compact_with_reopen_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // insert documents
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // remove doc
    fdb_doc_create(&rdoc, doc[1]->key, doc[1]->keylen, doc[1]->meta, doc[1]->metalen, NULL, 0);
    rdoc->deleted = true;
    fdb_set(db, rdoc);
    fdb_doc_free(rdoc);

    // commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // perform compaction using one handle
    fdb_compact(dbfile, (char *) "./dummy2");

    // close db file
    fdb_kvs_close(db);
    fdb_close(dbfile);

    r = system(SHELL_MOVE " dummy2 dummy1 > errorlog.txt");
    (void)r;
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compact_with_reopen_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // retrieve documents using the other handle without close/re-open
    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);

        if (i != 1) {
            TEST_CHK(status == FDB_RESULT_SUCCESS);
            TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
            TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        }else{
            TEST_CHK(status == FDB_RESULT_KEY_NOT_FOUND);
        }

        // free result document
        fdb_doc_free(rdoc);
    }
    // check the other handle's filename
    fdb_file_info info;
    fdb_get_file_info(dbfile, &info);
    TEST_CHK(!strcmp("./dummy1", info.filename));

    // update documents
    for (i=0;i<n;++i){
        sprintf(metabuf, "newmeta%d", i);
        sprintf(bodybuf, "newbody%d_%s", i, temp);
        fdb_doc_update(&doc[i], (void *)metabuf, strlen(metabuf),
                       (void *)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // Open the database with another handle.
    fdb_file_handle *second_dbfile;
    fdb_kvs_handle *second_dbh;
    fdb_open(&second_dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(second_dbfile, &second_dbh, &kvs_config);
    status = fdb_set_log_callback(second_dbh, logCallbackFunc,
                                  (void *) "compact_with_reopen_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // In-place compactions with a handle still open on the first old file
    status = fdb_compact(dbfile, NULL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    status = fdb_compact(dbfile, NULL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // MB-12977: retest compaction again..
    status = fdb_compact(dbfile, NULL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    fdb_kvs_close(db);
    fdb_close(dbfile);

    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(second_dbh, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        // free result document
        fdb_doc_free(rdoc);
    }

    // Open database with an original name.
    status = fdb_open(&dbfile, "./dummy1", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compact_with_reopen_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    fdb_get_file_info(dbfile, &info);
    // The actual file name should be a compacted one.
    TEST_CHK(!strcmp("./dummy1.3", info.filename));

    fdb_kvs_close(second_dbh);
    fdb_close(second_dbfile);

    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        // free result document
        fdb_doc_free(rdoc);
    }

    fdb_compact(dbfile, NULL);
    fdb_kvs_close(db);
    fdb_close(dbfile);

    r = system(SHELL_MOVE " dummy1 dummy.fdb > errorlog.txt");
    (void)r;
    fdb_open(&dbfile, "./dummy.fdb", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compact_with_reopen_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // In-place compaction
    fdb_compact(dbfile, NULL);
    fdb_kvs_close(db);
    fdb_close(dbfile);
    // Open database with an original name.
    status = fdb_open(&dbfile, "./dummy.fdb", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compact_with_reopen_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    fdb_get_file_info(dbfile, &info);
    TEST_CHK(!strcmp("./dummy.fdb", info.filename));
    TEST_CHK(info.doc_count == 100);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    fdb_kvs_close(db);
    fdb_close(dbfile);

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("compaction with reopen test");
}

void compact_reopen_named_kvs()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 100;
    int nkvdocs;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_status status;
    fdb_kvs_info kvs_info;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open(dbfile, &db, "db",  &kvs_config);

    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compact_reopen_named_kvs");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // commit
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // compact
    fdb_compact(dbfile, NULL);

    // save ndocs
    fdb_get_kvs_info(db, &kvs_info);
    nkvdocs = kvs_info.doc_count;

    // close db file
    fdb_kvs_close(db);
    fdb_close(dbfile);

    // reopen
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open(dbfile, &db, "db",  &kvs_config);

    // verify kvs stats
    fdb_get_kvs_info(db, &kvs_info);
    TEST_CHK(nkvdocs == kvs_info.doc_count);

    fdb_kvs_close(db);
    fdb_close(dbfile);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("compact reopen named kvs");
}

void compact_upto_test(bool multi_kv)
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 20;
    int num_kvs = 4; // keep this the same as number of fdb_commit() calls
    fdb_file_handle *dbfile;
    fdb_kvs_handle **db = alca(fdb_kvs_handle *, num_kvs);
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_status status;
    fdb_snapshot_info_t *markers;
    fdb_kvs_handle *snapshot;
    uint64_t num_markers;

    char keybuf[256], metabuf[256], bodybuf[256];
    char kv_name[8];
    char compact_filename[16];

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;
    fconfig.multi_kv_instances = multi_kv;

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    if (multi_kv) {
        for (r = 0; r < num_kvs; ++r) {
            sprintf(kv_name, "kv%d", r);
            fdb_kvs_open(dbfile, &db[r], kv_name, &kvs_config);
        }
    } else {
        num_kvs = 1;
        fdb_kvs_open_default(dbfile, &db[0], &kvs_config);
    }

   // ------- Setup test ----------------------------------
   // insert documents of 0-4
    for (i=0; i<n/4; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        for (r = 0; r < num_kvs; ++r) {
            fdb_set(db[r], doc[i]);
        }
    }

    // commit with a manual WAL flush (these docs go into HB-trie)
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 5 - 9
    for (; i < n/2; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        for (r = 0; r < num_kvs; ++r) {
            fdb_set(db[r], doc[i]);
        }
    }

    // commit again without a WAL flush
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // insert documents from 10-14 into HB-trie
    for (; i < (n/2 + n/4); i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        for (r = 0; r < num_kvs; ++r) {
            fdb_set(db[r], doc[i]);
        }
    }
    // manually flush WAL & commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 15 - 19 on file into the WAL
    for (; i < n; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        for (r = 0; r < num_kvs; ++r) {
            fdb_set(db[r], doc[i]);
        }
    }
    // commit without a WAL flush
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    for (r = 0; r < num_kvs; ++r) {
        status = fdb_set_log_callback(db[r], logCallbackFunc,
                                      (void *) "compact_upto_test");
        TEST_CHK(status == FDB_RESULT_SUCCESS);
    }

    status = fdb_get_all_snap_markers(dbfile, &markers, &num_markers);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    if (!multi_kv) {
        TEST_CHK(num_markers == 4);
        for (r = 0; r < num_markers; ++r) {
            TEST_CHK(markers[r].num_kvs_markers == 1);
            TEST_CHK(markers[r].kvs_markers[0].seqnum == (n - r*5));
        }
        r = 1; // Test compacting upto sequence number 15
        sprintf(compact_filename, "dummy_compact%d", r);
        status = fdb_compact_upto(dbfile, compact_filename,
                                  markers[r].marker);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        // create a snapshot
        status = fdb_snapshot_open(db[0], &snapshot,
                                   markers[r].kvs_markers[0].seqnum);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        // close snapshot
        fdb_kvs_close(snapshot);
    } else {
        TEST_CHK(num_markers == 8);
        for (r = 0; r < num_kvs; ++r) {
            TEST_CHK(markers[r].num_kvs_markers == num_kvs);
            for (i = 0; i < num_kvs; ++i) {
                TEST_CHK(markers[r].kvs_markers[i].seqnum == (n - r*5));
                sprintf(kv_name, "kv%d", i);
                TEST_CMP(markers[r].kvs_markers[i].kv_store_name, kv_name, 3);
            }
        }
        i = r = 1;
        sprintf(compact_filename, "dummy_compact%d", i);
        status = fdb_compact_upto(dbfile, compact_filename,
                markers[i].marker);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        // create a snapshot
        status = fdb_snapshot_open(db[r], &snapshot,
                markers[i].kvs_markers[r].seqnum);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        // close snapshot
        fdb_kvs_close(snapshot);
    }

    status = fdb_free_snap_markers(markers, num_markers);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // close db file
    fdb_close(dbfile);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // free all resources
    fdb_shutdown();

    memleak_end();

    sprintf(bodybuf, "compact upto marker in file test %s", multi_kv ?
                                                           "multiple kv mode:"
                                                         : "single kv mode:");
    TEST_RESULT(bodybuf);
}

void auto_recover_compact_ok_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 3;
    fdb_file_handle *dbfile, *dbfile_new;
    fdb_kvs_handle *db;
    fdb_kvs_handle *db_new;
    fdb_doc **doc = alca(fdb_doc *, n);
    fdb_doc *rdoc;
    fdb_status status;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL " dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 16777216;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "auto_recover_compact_ok_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    fdb_open(&dbfile_new, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile_new, &db_new, &kvs_config);
    status = fdb_set_log_callback(db_new, logCallbackFunc,
                                  (void *) "auto_recover_compact_ok_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // insert first two documents
    for (i=0;i<2;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // remove second doc
    fdb_doc_create(&rdoc, doc[1]->key, doc[1]->keylen, doc[1]->meta, doc[1]->metalen, NULL, 0);
    rdoc->deleted = true;
    fdb_set(db, rdoc);
    fdb_doc_free(rdoc);

    // commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // perform compaction using one handle
    fdb_compact(dbfile, (char *) "./dummy2");

    // save the old file after compaction is done ..
    r = system(SHELL_COPY " dummy1 dummy11 > errorlog.txt");
    (void)r;

    // now insert third doc: it should go to the newly compacted file.
    sprintf(keybuf, "key%d", i);
    sprintf(metabuf, "meta%d", i);
    sprintf(bodybuf, "body%d", i);
    fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
        (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
    fdb_set(db, doc[i]);

    // commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // close both the db files ...
    fdb_kvs_close(db);
    fdb_kvs_close(db_new);
    fdb_close(dbfile);
    fdb_close(dbfile_new);

    // restore the old file after close is done ..
    r = system(SHELL_MOVE " dummy11 dummy1 > errorlog.txt");
    (void)r;

    // now open the old saved compacted file, it should automatically recover
    // and use the new file since compaction was done successfully
    fdb_open(&dbfile_new, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile_new, &db_new, &kvs_config);
    status = fdb_set_log_callback(db_new, logCallbackFunc,
                                  (void *) "auto_recover_compact_ok_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // retrieve documents using the old handle and expect all 3 docs
    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db_new, rdoc);

        if (i != 1) {
            TEST_CHK(status == FDB_RESULT_SUCCESS);
            TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
            TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        }else{
            TEST_CHK(status == FDB_RESULT_KEY_NOT_FOUND);
        }

        // free result document
        fdb_doc_free(rdoc);
    }
    // check this handle's filename it should point to newly compacted file
    fdb_file_info info;
    fdb_get_file_info(dbfile_new, &info);
    TEST_CHK(!strcmp("./dummy2", info.filename));

    // close the file
    fdb_kvs_close(db_new);
    fdb_close(dbfile_new);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("auto recovery after compaction test");
}

void db_compact_overwrite()
{
    TEST_INIT();
    memleak_start();

    int i, r;
    int n = 30;
    fdb_file_handle *dbfile, *dbfile2;
    fdb_kvs_handle *db, *db2;
    fdb_doc **doc = alca(fdb_doc *, n);
    fdb_doc **doc2 = alca(fdb_doc *, 2*n);
    fdb_doc *rdoc;
    fdb_status status;
    fdb_config fconfig;
    fdb_kvs_info kvs_info;
    fdb_kvs_config kvs_config;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL " dummy* > errorlog.txt");
    (void)r;

    fconfig = fdb_get_default_config();
    kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 16777216;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // write to db1
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open(dbfile, &db, NULL, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "db_destroy_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // Open the empty db with future compact name
    fdb_open(&dbfile2, "./dummy1.1", &fconfig);
    fdb_kvs_open(dbfile2, &db2, NULL, &kvs_config);
    status = fdb_set_log_callback(db2, logCallbackFunc,
                                  (void *) "db_destroy_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // write to db2
    for (i=0;i < 2*n;++i){
        sprintf(keybuf, "k2ey%d", i);
        sprintf(metabuf, "m2eta%d", i);
        sprintf(bodybuf, "b2ody%d", i);
        fdb_doc_create(&doc2[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db2, doc2[i]);
    }
    fdb_commit(dbfile2, FDB_COMMIT_NORMAL);


    // verify db2 seqnum and close
    fdb_get_kvs_info(db2, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum = 2*n);
    fdb_kvs_close(db2);
    fdb_close(dbfile2);

    // compact db1
    status = fdb_compact(dbfile, NULL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // close db1
    fdb_kvs_close(db);
    fdb_close(dbfile);

    // reopen db1
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open(dbfile, &db, NULL, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "db_destroy_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // read db1
    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CHK(!memcmp(rdoc->meta, doc[i]->meta, rdoc->metalen));
        TEST_CHK(!memcmp(rdoc->body, doc[i]->body, rdoc->bodylen));
        // free result document
        fdb_doc_free(rdoc);
    }

    // reopen db2
    fdb_open(&dbfile2, "./dummy1.1", &fconfig);
    fdb_kvs_open(dbfile2, &db2, NULL, &kvs_config);
    status = fdb_set_log_callback(db2, logCallbackFunc,
                                  (void *) "db_destroy_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    fdb_get_kvs_info(db2, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum = 2*n);

    // read db2
    for (i=0;i<2*n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc2[i]->key, doc2[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db2, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CHK(!memcmp(rdoc->meta, doc2[i]->meta, rdoc->metalen));
        TEST_CHK(!memcmp(rdoc->body, doc2[i]->body, rdoc->bodylen));
        // free result document
        fdb_doc_free(rdoc);
    }

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
        fdb_doc_free(doc2[i]);
    }
    for (i=n;i<2*n;++i){
        fdb_doc_free(doc2[i]);
    }

    fdb_kvs_close(db);
    fdb_close(dbfile);
    fdb_kvs_close(db2);
    fdb_close(dbfile2);


    fdb_shutdown();
    memleak_end();
    TEST_RESULT("compact overwrite");
}

void *db_compact_during_doc_delete(void *args)
{

    TEST_INIT();
    memleak_start();

    int i, r;
    int n = 100;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db;
    fdb_status status;
    fdb_config fconfig;
    fdb_kvs_config kvs_config;
    fdb_kvs_info kvs_info;
    thread_t tid;
    void *thread_ret;
    fdb_doc **doc = alca(fdb_doc*, n);
    char keybuf[256], metabuf[256], bodybuf[256];

    if (args == NULL)
    { // parent

        r = system(SHELL_DEL" dummy* > errorlog.txt");
        (void)r;
        // init dbfile
        kvs_config = fdb_get_default_kvs_config();
        fconfig = fdb_get_default_config();
        fconfig.buffercache_size = 0;
        fconfig.wal_threshold = 1024;
        fconfig.compaction_threshold = 0;

        status = fdb_open(&dbfile, "./dummy1", &fconfig);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        // insert documents
        for (i=0;i<n;++i){
            sprintf(keybuf, "key%d", i);
            sprintf(metabuf, "meta%d", i);
            sprintf(bodybuf, "body%d", i);
            fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
                (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
            fdb_set(db, doc[i]);
        }

        fdb_commit(dbfile, FDB_COMMIT_NORMAL);
        // verify no docs remaining
        fdb_get_kvs_info(db, &kvs_info);
        TEST_CHK(kvs_info.doc_count == n);

        // start deleting docs
        for (i=0;i<n;++i){
            fdb_del(db, doc[i]);
            if (i == n/2){
                // compact half-way
                thread_create(&tid, db_compact_during_doc_delete, (void *)dbfile);
            }
        }

        // join compactor
        thread_join(tid, &thread_ret);

        // verify no docs remaining
        fdb_get_kvs_info(db, &kvs_info);
        TEST_CHK(kvs_info.doc_count == 0);

        // reopen
        fdb_kvs_close(db);
        fdb_close(dbfile);
        status = fdb_open(&dbfile, "./dummy1", &fconfig);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        fdb_get_kvs_info(db, &kvs_info);
        TEST_CHK(kvs_info.doc_count == 0);

        // cleanup
        for (i=0;i<n;++i){
            fdb_doc_free(doc[i]);
        }
        fdb_kvs_close(db);
        fdb_close(dbfile);
        fdb_shutdown();

        memleak_end();
        TEST_RESULT("multi thread client shutdown");
        return NULL;
    }

    // threads enter here //
    dbfile = (fdb_file_handle *)args;
    status = fdb_compact(dbfile, NULL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // shutdown
    thread_exit(0);
    return NULL;
}

void compaction_daemon_test(size_t time_sec)
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 10000;
    int compaction_threshold = 30;
    int escape = 0;
    fdb_file_handle *dbfile, *dbfile_less, *dbfile_non, *dbfile_manual, *dbfile_new;
    fdb_kvs_handle *db, *db_less, *db_non, *db_manual;
    fdb_kvs_handle *snapshot;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc;
    fdb_file_info info;
    fdb_status status;
    struct timeval ts_begin, ts_cur, ts_gap;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_mode = FDB_COMPACTION_AUTO;
    fconfig.compaction_threshold = compaction_threshold;
    fconfig.compactor_sleep_duration = 1; // for quick test

    // open db
    fdb_open(&dbfile, "dummy", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compaction_daemon_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // insert documents
    printf("Initialize..\n");
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%04d", i);
        sprintf(metabuf, "meta%04d", i);
        sprintf(bodybuf, "body%04d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf)+1,
            (void*)metabuf, strlen(metabuf)+1, (void*)bodybuf, strlen(bodybuf)+1);
        fdb_set(db, doc[i]);
    }
    // commit
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    // close db file
    fdb_close(dbfile);

    // ---- basic retrieve test ------------------------
    // reopen db file
    status = fdb_open(&dbfile, "dummy", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compaction_daemon_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // check db filename
    fdb_get_file_info(dbfile, &info);
    TEST_CHK(!strcmp(info.filename, "dummy"));

    // retrieve documents
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%04d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf)+1,
            NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        //printf("%s %s\n", rdoc->key, rdoc->body);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        fdb_doc_free(rdoc);
    }

    // create a snapshot
    status = fdb_snapshot_open(db, &snapshot, n);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // close snapshot
    fdb_kvs_close(snapshot);

    // close db file
    fdb_close(dbfile);

    // ---- handling when metafile is removed ------------
    // remove meta file
    r = system(SHELL_DEL" dummy.meta > errorlog.txt");
    (void)r;
    // reopen db file
    status = fdb_open(&dbfile, "dummy", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compaction_daemon_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // retrieve documents
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%04d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf)+1,
            NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        //printf("%s %s\n", rdoc->key, rdoc->body);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        fdb_doc_free(rdoc);
    }
    // close db file
    fdb_close(dbfile);

    // ---- handling when metafile points to non-exist file ------------
    // remove meta file
    r = system(SHELL_MOVE" dummy.0 dummy.23 > errorlog.txt");
    (void)r;
    // reopen db file
    status = fdb_open(&dbfile, "dummy", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compaction_daemon_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // retrieve documents
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%04d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf)+1,
            NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        //printf("%s %s\n", rdoc->key, rdoc->body);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        fdb_doc_free(rdoc);
    }
    // close db file
    fdb_close(dbfile);

    // ---- compaction daemon test -------------------
    // db: DB instance to be compacted
    // db_less: DB instance to be compacted but with much lower update throughput
    // db_non: DB instance not to be compacted (auto compaction with threshold = 0)
    // db_manual: DB instance not to be compacted (manual compaction)

    // open & create db_less, db_non and db_manual
    status = fdb_open(&dbfile_less, "dummy_less", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile_less, &db_less, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    fconfig.compaction_threshold = 0;
    status = fdb_open(&dbfile_non, "dummy_non", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile_non, &db_non, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    fconfig.compaction_mode = FDB_COMPACTION_MANUAL;
    status = fdb_open(&dbfile_manual, "dummy_manual", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile_manual, &db_manual, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // reopen db file
    fconfig.compaction_threshold = 30;
    fconfig.compaction_mode = FDB_COMPACTION_AUTO;
    status = fdb_open(&dbfile, "dummy", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "compaction_daemon_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // continuously update documents
    printf("wait for %d seconds..\n", (int)time_sec);
    gettimeofday(&ts_begin, NULL);
    while (!escape) {
        for (i=0;i<n;++i){
            // update db
            fdb_set(db, doc[i]);
            fdb_commit(dbfile, FDB_COMMIT_NORMAL);

            // update db_less (1/100 throughput)
            if (i%100 == 0){
                fdb_set(db_less, doc[i]);
                fdb_commit(dbfile_less, FDB_COMMIT_NORMAL);
            }

            // update db_non
            fdb_set(db_non, doc[i]);
            fdb_commit(dbfile_non, FDB_COMMIT_NORMAL);

            // update db_manual
            fdb_set(db_manual, doc[i]);
            fdb_commit(dbfile_manual, FDB_COMMIT_NORMAL);

            gettimeofday(&ts_cur, NULL);
            ts_gap = _utime_gap(ts_begin, ts_cur);
            if (ts_gap.tv_sec >= time_sec) {
                escape = 1;
                break;
            }
        }
    }

    // perform manual compaction of auto-compact file
    status = fdb_compact(dbfile_non, NULL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // perform manual compaction of manual-compact file
    status = fdb_compact(dbfile_manual, "dummy_manual_compacted");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // open dummy_manual_compacted using new db handle
    fconfig.compaction_mode = FDB_COMPACTION_MANUAL;
    status = fdb_open(&dbfile_new, "dummy_manual_compacted", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // try to switch compaction mode
    status = fdb_switch_compaction_mode(dbfile_manual, FDB_COMPACTION_AUTO, 30);
    TEST_CHK(status == FDB_RESULT_FILE_IS_BUSY);

    // close db_new
    status = fdb_close(dbfile_new);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // switch compaction mode of 'db_manual' from MANUAL to AUTO
    status = fdb_switch_compaction_mode(dbfile_manual, FDB_COMPACTION_AUTO, 10);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // change compaction value
    status = fdb_switch_compaction_mode(dbfile_manual, FDB_COMPACTION_AUTO, 30);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // close and open with auto-compact option
    status = fdb_close(dbfile_manual);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    fconfig.compaction_mode = FDB_COMPACTION_AUTO;
    status = fdb_open(&dbfile_manual, "dummy_manual_compacted", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // switch compaction mode of 'db_non' from AUTO to MANUAL
    status = fdb_switch_compaction_mode(dbfile_non, FDB_COMPACTION_MANUAL, 0);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // close and open with manual-compact option
    status = fdb_close(dbfile_non);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    fconfig.compaction_mode = FDB_COMPACTION_MANUAL;
    status = fdb_open(&dbfile_non, "dummy_non", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Now perform one manual compaction on dummy_non
    fdb_compact(dbfile_non, "dummy_non.manual");

    // close all db files except dummy_non
    fdb_close(dbfile);
    fdb_close(dbfile_less);
    fdb_close(dbfile_manual);

    // open manual compact file (dummy_non) using auto compact mode
    fconfig.compaction_mode = FDB_COMPACTION_AUTO;
    status = fdb_open(&dbfile, "dummy_non.manual", &fconfig);
    TEST_CHK(status == FDB_RESULT_INVALID_COMPACTION_MODE);

    // Attempt to destroy manual compact file using auto compact mode
    status = fdb_destroy("dummy_non.manual", &fconfig);
    TEST_CHK(status == FDB_RESULT_INVALID_COMPACTION_MODE);

    // open auto copmact file (dummy_manual_compacted) using manual compact mode
    fconfig.compaction_mode = FDB_COMPACTION_MANUAL;
    status = fdb_open(&dbfile, "dummy_manual_compacted", &fconfig);
    TEST_CHK(status == FDB_RESULT_INVALID_COMPACTION_MODE);

    // Attempt to destroy auto copmact file using manual compact mode
    status = fdb_destroy("dummy_manual_compacted", &fconfig);
    TEST_CHK(status == FDB_RESULT_INVALID_COMPACTION_MODE);

    // DESTROY auto copmact file with correct mode
    fconfig.compaction_mode = FDB_COMPACTION_AUTO;
    status = fdb_destroy("dummy_manual_compacted", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // DESTROY manual compacted file with past version open!
    fconfig.compaction_mode = FDB_COMPACTION_MANUAL;
    status = fdb_destroy("dummy_non.manual", &fconfig);
    TEST_CHK(status == FDB_RESULT_FILE_IS_BUSY);

    // Simulate a database crash by doing a premature shutdown
    // Note that db_non was never closed properly
    status = fdb_shutdown();
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    status = fdb_destroy("dummy_non.manual", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Attempt to read-only auto compacted and destroyed file
    fconfig.flags = FDB_OPEN_FLAG_RDONLY;
    status = fdb_open(&dbfile, "./dummy_manual_compacted", &fconfig);
    TEST_CHK(status == FDB_RESULT_NO_SUCH_FILE);

    status = fdb_open(&dbfile, "./dummy_manual_compacted.meta", &fconfig);
    TEST_CHK(status == FDB_RESULT_NO_SUCH_FILE);

    // Attempt to read-only past version of manually compacted destroyed file
    status = fdb_open(&dbfile, "dummy_non", &fconfig);
    TEST_CHK(status == FDB_RESULT_NO_SUCH_FILE);

    // Attempt to read-only current version of manually compacted destroyed file
    status = fdb_open(&dbfile, "dummy_non.manual", &fconfig);
    TEST_CHK(status == FDB_RESULT_NO_SUCH_FILE);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("compaction daemon test");
}

// MB-13117
void auto_compaction_with_concurrent_insert_test(size_t t_limit)
{
    TEST_INIT();

    memleak_start();

    int i, r;
    fdb_file_handle *file;
    fdb_kvs_handle *kvs;
    fdb_status status;
    fdb_config config;
    fdb_kvs_config kvs_config;
    struct timeval ts_begin, ts_cur, ts_gap;

    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // Open Database File
    config = fdb_get_default_config();
    config.compaction_mode=FDB_COMPACTION_AUTO;
    config.compactor_sleep_duration = 1;
    status = fdb_open(&file, "dummy", &config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Open KV Store
    kvs_config = fdb_get_default_kvs_config();
    status = fdb_kvs_open_default(file, &kvs, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    gettimeofday(&ts_begin, NULL);
    printf("wait for %d seconds..\n", (int)t_limit);

    // Several kv pairs
    for(i=0;i<100000;i++) {
        char str[15];
        sprintf(str, "%d", i);
        status = fdb_set_kv(kvs, str, strlen(str), (void*)"value", 5);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        // Commit
        status = fdb_commit(file, FDB_COMMIT_NORMAL);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        gettimeofday(&ts_cur, NULL);
        ts_gap = _utime_gap(ts_begin, ts_cur);
        if (ts_gap.tv_sec >= t_limit) {
            break;
        }
    }

    status = fdb_close(file);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_shutdown();
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    memleak_end();

    TEST_RESULT("auto compaction with concurrent insert test");
}

int main(){
    compact_wo_reopen_test();
    compact_with_reopen_test();
    compact_reopen_named_kvs();
    compact_upto_test(false); // single kv instance in file
    compact_upto_test(true); // multiple kv instance in file
    auto_recover_compact_ok_test();
    db_compact_overwrite();
    db_compact_during_doc_delete(NULL);
    compaction_daemon_test(20);
    auto_compaction_with_concurrent_insert_test(20);

    return 0;
}

