/**
 * Copyright(c) 2021 PeachyDB inc
 *
 * peachydb_client_separate.cpp
 */

/*
 * This test utilizes separate fibers for reads and writes otherwise it is nearly
 * identical to peachydb_client_cqldb.cpp. This may be a better approach. Since
 * reads and writes have completely different performance behavior on the servers,
 * this may make it easier to debug any issues.
 */

#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <pthread.h>

#include <boost/fiber/all.hpp>

#include "peachdb_test.hpp"
#include "cqldb/cqldb_api.h"
#include "fast_rand.h"

//extern int g_num_flips_performed;

//some globals related to the client info
static const char* cluster_configuration_path = "/dbstore/mymisc/cluster_config.info";
static const char* self_configuration_path = "/dbstore/mymisc/my_config.info";

using namespace std;

typedef struct __client_thrd_arg__ {
  int client_id;
  int thrd_id;
  int fiber_id;
  int test_range_queries; /* optional to alter test behavior */

  int larger_records;     /* for AWS testing we will utilize larger records to
                           * reach the database cache limits faster, for testing
                           * out of cache load performance more effectively */
} client_thrd_arg_t;

const char* g_fiber_tag = "Fiber:";
const char* g_thread_tag = "Thread:";

static int g_num_active_wr_fibers=0;
static int g_num_active_rd_fibers=0;

#define __START_WR_FIBER (__sync_fetch_and_add(&g_num_active_wr_fibers, 1))
#define __LEAVE_WR_FIBER (__sync_fetch_and_sub(&g_num_active_wr_fibers, 1))
#define __NUM_WR_FIBERS  (__sync_fetch_and_add(&g_num_active_wr_fibers, 0))

#define __START_RD_FIBER (__sync_fetch_and_add(&g_num_active_rd_fibers, 1))
#define __LEAVE_RD_FIBER (__sync_fetch_and_sub(&g_num_active_rd_fibers, 1))
#define __NUM_RD_FIBERS  (__sync_fetch_and_add(&g_num_active_rd_fibers, 0))

/*
 * __check_resubmit_write
 * if a write query timed out it may be because:
 *  -- write was submitted to cluster but did not complete on time, in
 *    this case we do not want to resubmit write.
 *  -- write was never submitted to cluster, in this case we want to
 *    resubmit the write request
 * stat - the statement that has just been executed.
 *
 * IMPORTANT:: this check must be performed ONLY for WRITE requests
 * not for any read requests. This information is tracked only for
 * WRITE requests in the cluster and can only be provided for the
 * immediately previous write submitted by the client thread.
 *
 * IMPORTANT:: the status can be checked within 10 minutes of submission
 * of the original request. Beyond that point the request info may have been
 * garbage collected
 */
static int
__check_resubmit_write(cqldb::statement& stat)
{
  int resubmit=0, wrstatus;

  //quick check to see if we got a response or not, if we got a response
  //then we are not going to resubmit the request. DO NOT make the expensive
  //round trip to the cluster co-ordinators to find out the results of the
  //request. The caller should check this, but we do it anyway to make sure
  //we don't waste effort.
  if(stat.got_response()) {
    return resubmit;
  }

  //wr_timedout_status() is an expensive request that makes a loop back
  //to the cluster to determine whether or not the request was submitted.
  //it must not be invoked multiple times for each write execution.
  try {
    //timeout can be set on request check as well, just in case
    //occasionally it can happen that the leader incurs a timeout
    //and the client automatically submits the request to a different
    //co-ordinator which may not have the answer yet.
    stat.set_timeout(5);

    //IMPORTANT:: This API returns status of immediately prior request
    //submitted to the cluster. Invoking this on a request older than the
    //most recent request will return an incorrect result.
    wrstatus = stat.wr_timedout_status();
  } catch(exception const &e) {
    cout << "Attempt to check wr_timedout_status on read-only request?" << endl;
    return 0;
  }

  switch(wrstatus) {
    case CQLDB_WR_STATUS_NOT_SUBMITTED:
     cout << "Resubmitting timedout WRITE request" << endl;
     resubmit = 1;
     break;
    case CQLDB_WR_STATUS_SUBMITTED:
     resubmit = 0;
     break;
    case CQLDB_WR_STATUS_UNKNOWN:
     //IMPORTANT CUSTOMER NOTE::
     //This can happen if there is a power failure and the cluster has lost
     //in memory information about whether the request was submitted or not
     //in this case the Customer must provide code (database read query to execute)
     //to determine whether or not the transaction was applied to the cluster.
     //
     //It may also happen due to uncommon timing issues. For such cases perhaps
     //we should retry status checking a few times, with 100 miliseconds sleep in
     //between? When the status returned remains unknown, we may have no option
     //but to submit a query against the database to determine if the data is
     //actually present or not. This is where the customer has to provide
     //a query to check if the data is present in the database or not.
     //
     //Hopefully the response to the query will indicate what happened. The reason
     //for 'hopefully' is that due to snapshot isolation it is possible that reads
     //will return data that is not most recently committed. Maybe non-snapshot
     //isolation request will work better in this case. For this test we will do
     //the safer thing and not resubmit the request
     //
     cout << "Unknown Status of Timedout WRITE request,"
          << " should we do something about it?" << endl;
     break;
  }
  if(resubmit) {
    //if previous request timed out, could this be due to load on server? should
    //we add a slight delay to allow the server a breathing room?
    //usleep(50) -- maybe 50 microseconds delay? maybe more? maybe less?
  }
  return resubmit;
}

/*
 * __peachdbtest_setup_schema
 * setup the schema, security data, initial data
 * NOTE:: if full_schema==1 then all indexes and materialized views will be
 * created, which will slow down the write performance by a lot. For a lot
 * of usages we should be utilizing full_schema==0.  (for client driver
 * peachtest_driver_thread() we can utilize full schema to test functionality).
 * NOTE:: this function is not thread safe. it should be invoked to load
 * schema into the database before any client threads are launched.
 */
static int
__peachtest_setup_schema(int full_schema)
{
  char qbuff[8192];
  char* qstr = qbuff;
  int numres, numr;
  FILE* schemaq = NULL;
  FILE* securityq = NULL;
  int schema_loaded=0;

  cout << "Setting up initial schema/security/data" << endl;

  if(full_schema) {
    schemaq = fopen("./peachdb_testfiles/peach.schema.full", "r");
  } else {
    schemaq = fopen("./peachdb_testfiles/peach.schema", "r");
  }
  securityq = fopen("./peachdb_testfiles/peach.security", "r");

  if(!schemaq || !securityq) {
    printf("schema files not found\n");
    exit(1);
  }

  try {
   //session must be created before invoking the driver
   ostringstream session_params;
   session_params << "peachdb_cluster:"
                  << "cluster-configuration-path=" << cluster_configuration_path << ";"
                  << "self-configuration-path=" << self_configuration_path;

   //setup a clustered peachdb session, NOTE: session handles are not
   //thread safe, nor are any of the statement or any other handles.
   cqldb::session cqldb_session = cqldb::session(session_params.str());

   while(schema_loaded<=1) {
     if(schema_loaded==0) {
       /* firstly we load the table schema */
       if(test_get_next_query(schemaq, qbuff, sizeof(qbuff), &numres)!=0) {
         schema_loaded++;
         cout << "completed loading schema" << endl;
         continue;
       }
     } else if(schema_loaded==1) {
       /* next we load the security schema */
       if(test_get_next_query(securityq, qbuff, sizeof(qbuff), &numres)!=0) {
         cout << "completed loading security schema" << endl;
         /* we have to update the client info with schema changes before we
          * can perform any queries that involve the schema, this is an
          * expensive operation and should not be executed often */
         if(cqldb_session.refresh_metadata()!=0) {
           cerr << "unable to obtain refresh meta info" << endl;
           exit(-1);
         }
         schema_loaded++;
         continue;
       }
     }
     try {
       cqldb::statement stat;
       stat = cqldb_session << qstr;
       do {
         //each iteration in this loop resubmits request to cluster
         stat.exec();
       } while(!stat.got_response() && __check_resubmit_write(stat));
       stat.reset(); //release statement resources, else memory leaks/corruption
     } catch(exception const &e) {
       cerr << "ERROR: " << e.what() << endl;
     }
   }
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
  }
  fclose(schemaq);
  schemaq = NULL;
  fclose(securityq);
  securityq = NULL;
done:
  return 0;
}

/* 
 * __ceiling_power_of_two
 * return the least-upper-bound power of 2 of the number provided
 */
static uint32_t
__ceiling_power_of_two(uint32_t n)
{
  n = n-1;
  n |= (n>>1);
  n |= (n>>2); 
  n |= (n>>4);
  n |= (n>>8);
  n |= (n>>16);
  return n+1;
}

/*
 * __COMPUTE_HASH
 * we are limiting the number of clients to 64k and the number of fibers per
 * client to 64k
 */
#define __COMPUTE_HASH(__clientid, __fiberid, __iternum, __hashval) do {\
 uint64_t __key;\
 assert((__clientid)<0xffff);\
 assert((__fiberid)<0xffff);\
 __key = ((__clientid) & (0xffff)); __key<<=16; __key |= ((__fiberid) & 0xffff); __key<<=32; __key |= (__iternum);\
 __key = (__key ^ (__key >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);\
 __key = (__key ^ (__key >> 27)) * UINT64_C(0x94d049bb133111eb);\
 __key = (__key ^ (__key >> 31));\
 (__hashval) = (uint32_t)(__key & (0xffffffff));\
} while(0);

/* This will work if hash_table_size is a power of 2, for that case idx will be
 * the same as ((__hashval) % (__hash_table_size)), when setting the hash table
 * size we have to assert that the size is a power of 2. */
#define __COMPUTE_HASH_SLOT(__hashval, __hash_table_size) ((__hashval) & ((__hash_table_size)-1))

typedef struct {
  uint32_t  rval;
  uint8_t   f1len;
  uint8_t   f2len;
} data_tuple_t;

typedef struct {
 private:
  data_tuple_t*  arr;
  uint32_t       sz;

 public:

  /* init
   * initialize the table with values utilized to compose f1,f2, utilize a table
   * size which is closest power of 2 of num provided.
   * num - number of tuples
   */
  void init(uint32_t num)
  {
    sz = __ceiling_power_of_two(num);
    arr = (data_tuple_t*)malloc(num * sizeof(data_tuple_t));
    for(int i=0; i<sz; i++) {
      arr[i].rval = rand();
      arr[i].f1len = 7 + rand()%10;
      arr[i].f2len = 5 + rand()%10;
    }
  }

  /* compose_f1
   * given clientid and fiberid and iteration number compose f1 to be utilized
   */
  int compose_f1(int clientid, int fiberid, int iternum, char* f, int fsz)
  {
    int slot, i, rc=0;
    uint32_t hashval;

    __COMPUTE_HASH(clientid, fiberid, iternum, hashval);
    slot = __COMPUTE_HASH_SLOT(hashval, sz);
    assert(slot<sz);
    assert(arr[slot].f1len < fsz);

    //utilize clientid, fiberid to ensure different clients,threads generate
    //non-over lapping keys
    f[0] = (clientid & 0x0f) + 'a';
    f[1] = ((clientid>>4) & 0x0f) + 'a';

    //turn f[2], f[3], f[4], f[5] into characters 'a'-'z'
    f[2] = (fiberid & 0x0f) + 'a';
    f[3] = ((fiberid>>4) & 0x0f) + 'a';
    f[4] = ((fiberid>>8) & 0x0f) + 'a';
    f[5] = ((fiberid>>12) & 0x0f) + 'a';

    for(i=6; i<arr[slot].f1len; i++) {
      f[i] = 'A' + ((arr[slot].rval>>(i-6)) & 0x01f);
    }
    f[i] = '\0';
    return rc;
  }

  /* compose_f2
   * given clientid and fiberid and iteration number compose f2 to be utilized
   */
  int compose_f2(int clientid, int fiberid, int iternum, char* f, int fsz)
  {
    int slot, i, rc=0;
    uint32_t hashval;

    __COMPUTE_HASH(clientid, fiberid, iternum, hashval);
    slot = __COMPUTE_HASH_SLOT(hashval, sz);
    assert(slot<sz);
    assert(arr[slot].f2len < fsz);

    for(i=0; i<arr[slot].f2len; i++) {
      f[i] = 'z' - ((arr[slot].rval>>i) & 0x01f);
    }
    f[i] = '\0';
    return rc;
  }

} data_table_t;

#define __DEF_F1F2_TBLSZ (512*1024)

//table to generate f1 and f2
static data_table_t g_f1f2_table;

//to keep track of progress of write fibers
static int* g_write_progress;

//initialize write progress
#define __INIT_PROGRESS(__num_fibers) do {\
 g_write_progress = (int*)malloc((__num_fibers) * sizeof(int));\
} while(0);

//update write progress of specified fiberid, since update is performed
//by a single fiber thread we don't have to utilize atomics
#define __UPDATE_PROGRESS(__fiberid, __iter) do {\
 g_write_progress[(__fiberid)] = (__iter);\
} while(0);

//get write progress of specified fiberid, we utilize atomics due to
//race with updates
#define __GET_PROGRESS(__fiberid, __iter) do {\
 (__iter) = __sync_fetch_and_add(&g_write_progress[(__fiberid)], 0);\
} while(0);

//f3 is an int field simply obtained by (iteration + 100)
#define __COMPOSE_f3(__iter) ((__iter) + 100)

//compose fixed value for field f4
#define __COMPOSE_f4(__f4, __f4sz) do {\
 snprintf((__f4), (__f4sz), "10:01:34");\
} while(0);


/*
 * custom_mixload_driver_thread_writer
 * tries a mix of queries
 * - single writes
 * - batch writes
 */
static void
custom_mixload_driver_thread_writer(client_thrd_arg_t arg)
{
  int len, i, j;
  int chosen_index=0;
  int client_id = arg.client_id;
  int fiber_id = arg.fiber_id;
  int thrd_id = arg.thrd_id;
  int test_range_quries = arg.test_range_queries;
  const char* debug_tag = (fiber_id==-1)? g_thread_tag: g_fiber_tag;
  int debug_id = (fiber_id==-1)? thrd_id: fiber_id;
  int local_fiber_id = (fiber_id==-1)? thrd_id: fiber_id;
  uint64_t packets_sent, packets_recvd, packets_dropped;
  unsigned int thread_seed;

#define __MAXLEN  (20)
#define __NUM_UPD (12)
#define __NUM_DEL (12)

  char f1[__MAXLEN], f2[__MAXLEN], f4[__MAXLEN];
  char ip1[__MAXLEN], ip2[__MAXLEN];
#define __MAX_DESC 512 //maximum length of description field
  char descfield[__MAX_DESC];

  int  delete_iter[__NUM_DEL];
  char delete_f1[__MAXLEN], delete_f2[__MAXLEN], delete_f4[__MAXLEN];
  int  delete_count=0;
  int  delete_drain_count=0;

  int  update_iter[__NUM_UPD];
  char update_f1[__MAXLEN], update_f2[__MAXLEN], update_f4[__MAXLEN];
  int  update_count=0;
  int  update_drain_count=0;

  int write_batchsz, tmp_deldrain, tmp_upddrain, tmp_iter;
  int del_stats=0, upd_stats=0;

#define RANGE_TEST_THRESH (10000)
  int range_deletes=0;

  const char* insert_qstr = "INSERT INTO testkeyspace.test_table1 (field1, field2, field3, field4, field5, field6, description, listfield, setfield, mapfield) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

  //NOTE::updates and deletes that are part of a batch must restrict all primary key
  //columns with equality constraints. (updates, deletes that modify multiple
  //objects can not be part of a batched transaction).
  const char* update_qstr = "UPDATE testkeyspace.test_table1 SET listfield = ['todo', 'when'] + listfield WHERE field1 =? AND field2 =? AND field3 =? AND field4 =?";
  const char* delete_qstr = "DELETE FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =? AND field3 =? AND field4 =?";

  //for range deletes all partition key fields of base table as well as materialized views 
  //must be restricted.
  const char* delete_range_qstr = "DELETE FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =?";

  //per thread initialization
  fast_randseed();
  //utilize thread_seed for rand_r() instead of rand() to avoid futex calls
  thread_seed = time(NULL) ^ (debug_id << 16);
  __START_WR_FIBER;

  try {
    //session must be created before invoking the driver
    ostringstream session_params;
    session_params << "peachdb_cluster:"
                   << "cluster-configuration-path=" << cluster_configuration_path << ";"
                   << "self-configuration-path=" << self_configuration_path;

    //setup a clustered peachdb session, NOTE: session handles are not
    //thread safe, nor are any of the statement or any other handles.
    cqldb::session cqldb_session = cqldb::session(session_params.str());

    cout << debug_tag << debug_id << " kicking off query load" << endl;

    //BUG:: In some corner cases it is possible that preparing statements
    //outside the outermost for() loop below as well as within the loop can
    //lead to memory leak (ie preparing in both the places together, preparing
    //in only one spot is not an issue). If you observe this please report it.
    //
    //std::string write_batchstr = insert_qstr;
    //cqldb::statement stat = cqldb_session << insert_qstr;

    for(i=0; i<100000000; i++) {
      __UPDATE_PROGRESS(debug_id, i);

      g_f1f2_table.compose_f1(client_id, local_fiber_id, i, f1, __MAXLEN);
      g_f1f2_table.compose_f2(client_id, local_fiber_id, i, f2, __MAXLEN);
      __COMPOSE_f4(f4, sizeof(f4));

      snprintf(ip1, sizeof(ip1), "%d.%d.%d.%d",
               123, (((i>>16)+1)& 0xff), ((i>>8) & 0xff), (i & 0xff));
      snprintf(ip2, sizeof(ip2), "%d.%d.%d.%d",
               123, (((i>>16)+2)& 0xff), ((i>>8) & 0xff), (i & 0xff));

      std::string write_batchstr = "";
      write_batchstr = insert_qstr;

      if(delete_count==__NUM_DEL && update_count==__NUM_UPD) {
        write_batchsz = rand_r(&thread_seed) % (delete_count - delete_drain_count + update_count - update_drain_count);
        if(write_batchsz==0) {
          write_batchsz = 1;
        } else if(write_batchsz>5) {
          write_batchsz = 5;
        }
        tmp_deldrain = write_batchsz>>1;
        if(delete_drain_count < delete_count) {
          tmp_deldrain = tmp_deldrain % (delete_count - delete_drain_count);
          if(tmp_deldrain==0) {
            tmp_deldrain=1;
          }
        } else {
          tmp_deldrain = 0;
        }
        tmp_upddrain = write_batchsz - tmp_deldrain;
        if(update_drain_count < update_count) {
          if(tmp_upddrain > (update_count - update_drain_count)) {
            tmp_upddrain = (update_count - update_drain_count);
          }
        } else {
          tmp_upddrain = 0;
        }
        write_batchsz = tmp_upddrain + tmp_deldrain;
        for(j=0; j< tmp_deldrain; j++) {
          write_batchstr = (write_batchstr + ";") +  delete_qstr;
          del_stats++;
        }
        for(j=0; j< tmp_upddrain; j++) {
          write_batchstr = (write_batchstr + ";") + update_qstr;
          upd_stats++;
        }
      }

      //dynamically constructed queries have to be created within the loop because
      //we can not prepare them in advance outside the loop and hence we incurr the
      //cost of repeatedly preparing the queries.
      cqldb::statement stat = cqldb_session << write_batchstr;

      len = 1;
      if(arg.larger_records) {
        len = (fast_rand32() % __MAX_DESC) - 1;
        if(len < 256) {
          len = 256; //since we are attempting larger records utilize atleast 128 bytes.
        }
      }
      for(j=0; j<len; j++) {
        //caution::synthetic field may be artifically more compressible, if 
        //compression is utilized.
        descfield[j] = 'a' + (uint8_t)(fast_rand32() & 0xf);
      }
      descfield[j] = '\0';

      //NOTE::make certain that the parameters provided with query line up with
      //what is expected else you will get strange errors.
      cqldb::user_collection listf = stat.create_user_collection();
      listf << f1 << f2;
      cqldb::user_collection setf = stat.create_user_collection();
      setf << f1 << f2;
      cqldb::user_collection mapf = stat.create_user_collection();
      mapf << "key1" << f1 << "key2" << f2;

      //make sure to bind the parameters in correct order and count else you get
      //parameter binding errors, syntax errors etc.
      stat << f1 << f2 << __COMPOSE_f3(i) << f4 << ip1 <<ip2;
      stat << descfield << listf << setf << mapf;

      if(delete_count==__NUM_DEL && update_count==__NUM_UPD) {
        //bind the parameters for the delete and update queries
        for(j=0; j< tmp_deldrain; j++) {
          tmp_iter = delete_iter[delete_drain_count+j];
          g_f1f2_table.compose_f1(client_id, local_fiber_id, tmp_iter, delete_f1, __MAXLEN);
          g_f1f2_table.compose_f2(client_id, local_fiber_id, tmp_iter, delete_f2, __MAXLEN);
          __COMPOSE_f4(delete_f4, sizeof(delete_f4));
          stat << delete_f1 << delete_f2 << __COMPOSE_f3(tmp_iter) << delete_f4;
        }
        for(j=0; j< tmp_upddrain; j++) {
          tmp_iter = update_iter[update_drain_count+j];
          g_f1f2_table.compose_f1(client_id, local_fiber_id, tmp_iter, update_f1, __MAXLEN);
          g_f1f2_table.compose_f2(client_id, local_fiber_id, tmp_iter, update_f2, __MAXLEN);
          __COMPOSE_f4(update_f4, sizeof(update_f4));
          stat << update_f1 << update_f2 << __COMPOSE_f3(tmp_iter) << update_f4;
        }
        delete_drain_count += tmp_deldrain;
        update_drain_count += tmp_upddrain;
        if(delete_count==delete_drain_count && update_count==update_drain_count) {
          delete_count=0;
          delete_drain_count=0;
          update_count=0;
          update_drain_count=0;
        }
      }

      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      stat.set_timeout(120);
      do {
        //each iteration in this loop resubmits request to cluster
        stat.exec();
      } while(!stat.got_response() && __check_resubmit_write(stat));

      if(stat.got_response()) {
        try {
          cqldb::result res = stat.query();
          unsigned int flags;
          do {
            res.print_errors(flags);
            if(flags & CQLDB_HAS_LIMIT) {
              //if an update/delete modifies multiple objects there is a chance
              //that there are more objects that match the where clause than a
              //statement can update in a single transaction. In this case the
              //user can reissue the write query untill it no longer runs into
              //the limit (note, such updates/deletes can not be part of a batch)
              cout << debug_tag << debug_id << " write statement applied to limited(1000) objects" << endl;
            } else if(flags & CQLDB_WR_SKIPPED) {
              //it is possible that within a batch of writes an update/delete/insert
              //may not get applied due to key conflicts etc. In such cases that
              //statement in the batch will be skipped but the rest of the batch
              //will still be applied.
              cout << debug_tag << debug_id << " write statement skipped" << endl;
            }
            if(flags & CQLDB_LEADER_TIMEDOUT) {
              //this messages needs to be printed only once, but we are lazy and
              //we print it for each statement in the batch
              cout << debug_tag << debug_id << " leader timedout waiting to aggreate WRITE query results" << endl;
            }
            if(flags & CQLDB_CLIENT_TIMEDOUT) {
              //this messages needs to be printed only once, but we are lazy and
              //we print it for each statement in the batch
              cout << debug_tag << debug_id << " client timedout waiting on WRITE query results" << endl;
            }
          } while(res.next_stmt());
        } catch(exception const &e) {
          //we will not terminate, the client we will keep moving on
          //should we possibly resubmit the query? Customer could track
          //the number of such queries to make sure there arent a lot
          cerr << "ERROR: " << e.what() << endl;
        }
      }

      //always reset current statment before moving onto the next
      stat.reset(); //release statement resources, else memory leaks/corruption

      if(i%1000==0) {
        /* notice that for stats queries within a batch are counted individually
         * so a single batch of 4 inserts is counted as 4 queries */
        cout << debug_tag << debug_id << " mixed WR iter#:" << i
             << " update:" << upd_stats << " delete:" << del_stats
             << " range-del:" << range_deletes 
             << " WR-FIBERS:" << __NUM_WR_FIBERS
             << " RD-FIBERS:" << __NUM_RD_FIBERS;
             //<< " num-flips:" << g_num_flips_performed;
        cqldb_session.packet_stats(&packets_sent, &packets_recvd, &packets_dropped);
        cout << " sent:" << packets_sent << " recvd:" << packets_recvd
             << " dropped:" << packets_dropped << endl;
      }

      /* randomly decide if record is going to be later updated/deleted */
      switch(rand_r(&thread_seed)%8) {
       case 1:
         if(update_count < __NUM_UPD) {
           update_iter[update_count++] = i;
         }
         break;
       case 2:
         if(delete_count < __NUM_DEL) {
           delete_iter[delete_count++] = i;
         }
         break;
       default:
         break;
      }
      //NOTE:: This test is attempting to read data that has just been written
      //as a result these selects will not run into i/o and hence these select
      //tests do not stress random i/o at all

      if(test_range_quries && i>RANGE_TEST_THRESH) {
        //randomly decide if we are going to try a range-select or range-delete
        //we want to make these queries somewhat infrequent, so that the overall
        //test proceeds at a fair rate
        if(i%1000==0) {

          //dynamically constructed queries have to be created within the loop because
          //we can not prepare them in advance outside the loop and hence we incurr the
          //cost of repeatedly preparing the queries.
          cqldb::statement stat = cqldb_session << delete_range_qstr;

          chosen_index = (fast_rand32()) % i;

          g_f1f2_table.compose_f1(client_id, local_fiber_id, chosen_index, f1, __MAXLEN);
          stat << f1;
          g_f1f2_table.compose_f2(client_id, local_fiber_id, chosen_index, f2, __MAXLEN);
          stat << f2;

          //optionally set a timeout of 120 seconds on the statement, this may be
          //needed for queries that may be taking longer to execute. comment it
          //out if its not needed. timeout has to be between (120, 255)
          stat.set_timeout(120);
          do {
            //each iteration in this loop resubmits request to cluster
            stat.exec();
          } while(!stat.got_response() && __check_resubmit_write(stat));

          if(stat.got_response()) {
            try {
              cqldb::result res = stat.query();
              unsigned int flags;
              res.print_errors(flags);
              if(flags) {
                if(flags & CQLDB_HAS_LIMIT) {
                  cout << debug_tag << debug_id << " range delete ran into record limit" << endl;
                }
              }
            } catch(exception const &e) {
              //we will not terminate, the client we will keep moving on
              //should we possibly resubmit the query? Customer could track
              //the number of such queries to make sure there arent a lot
              cerr << "ERROR: " << e.what() << endl;
            }
          }
          stat.reset(); //release statement resources, else memory leaks/corruption
          range_deletes++;
        }
      }
    }
    cout << debug_tag << debug_id << " UPDATE #" << upd_stats << " DELETE #" << del_stats;
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
    goto done;
  }
done:
  __LEAVE_WR_FIBER;
  return;
}

/*
 * custom_mixload_driver_thread_reader
 * tries a mix of queries
 * - single reads
 * - batch reads
 */
static void
custom_mixload_driver_thread_reader(client_thrd_arg_t arg)
{
  int i, j;
  int chosen_index=0, iter;
  int client_id = arg.client_id;
  int fiber_id = arg.fiber_id;
  int thrd_id = arg.thrd_id;
  int test_range_quries = arg.test_range_queries;
  const char* debug_tag = (fiber_id==-1)? g_thread_tag: g_fiber_tag;
  int debug_id = (fiber_id==-1)? thrd_id: fiber_id;
  int wrfiber_id = ((debug_id)>>1);
  uint64_t packets_sent, packets_recvd, packets_dropped;
  unsigned int thread_seed;

#define __NUM_SEL (20)
  char f1[__MAXLEN], f2[__MAXLEN], f4[__MAXLEN];

  int  select_iter[__NUM_SEL];
  char select_f1[__MAXLEN], select_f2[__MAXLEN], select_f4[__MAXLEN];
  int  select_count=0;
  int  select_drain_count=0;
  int  sel_stats=0, no_results=0, tmp_iter;

#define RANGE_TEST_THRESH (10000)
  int range_selects=0;

  const char* select_qstr = "SELECT field1,field2,field3,field4,field5,field6,listfield,setfield,mapfield FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =? AND field3 =? AND field4 =?";

  const char* select_range_qstr = "SELECT field1,field2,field3,field4,field5,field6,listfield,setfield,mapfield FROM testkeyspace.test_table1 WHERE field1 =? LIMIT 200";

  //per thread initialization
  fast_randseed();
  //utilize thread_seed for rand_r() instead of rand() to avoid futex calls
  thread_seed = time(NULL) ^ (debug_id << 16);
  __START_RD_FIBER;

#define __YIELD(__usec) do {\
 if(fiber_id==-1) {\
  usleep((__usec));\
 } else {\
  boost::this_fiber::sleep_for(std::chrono::microseconds((__usec)));\
 }\
} while(0);

  try {
    //session must be created before invoking the driver
    ostringstream session_params;
    session_params << "peachdb_cluster:"
                   << "cluster-configuration-path=" << cluster_configuration_path << ";"
                   << "self-configuration-path=" << self_configuration_path;

    //setup a clustered peachdb session, NOTE: session handles are not
    //thread safe, nor are any of the statement or any other handles.
    cqldb::session cqldb_session = cqldb::session(session_params.str());

    cout << debug_tag << debug_id << " kicking off query load" << endl;

    for(i=0; i<100000000; i++) {

      //determine progress of the write fiber corresponding to the read fiber
      do {
        __GET_PROGRESS(wrfiber_id, j);
        if(j<=1) {
          __YIELD(10);
        }
      } while(j<=1);

      //determine randomly which iteration to query
      iter = fast_rand32() % j;

      g_f1f2_table.compose_f1(client_id, wrfiber_id, iter, f1, __MAXLEN);
      g_f1f2_table.compose_f2(client_id, wrfiber_id, iter, f2, __MAXLEN);
      __COMPOSE_f4(f4, sizeof(f4));

      //make sure to bind the parameters in correct order and count else you get
      //parameter binding errors, syntax errors etc.
      cqldb::statement stat = cqldb_session << select_qstr;
      stat << f1 << f2 << __COMPOSE_f3(iter) << f4;

      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      stat.set_timeout(120);
      do {
        //each iteration in this loop resubmits request to cluster
        stat.exec();
      } while(!stat.got_response());

      sel_stats++;

      if(stat.got_response()) {
        try {
          cqldb::result res = stat.query();
          unsigned int flags;
          do {
            res.print_errors(flags);
            if(flags & CQLDB_HAS_LIMIT) {
              //if an update/delete modifies multiple objects there is a chance
              //that there are more objects that match the where clause than a
              //statement can update in a single transaction. In this case the
              //user can reissue the write query untill it no longer runs into
              //the limit (note, such updates/deletes can not be part of a batch)
              cout << debug_tag << debug_id << " write statement applied to limited(1000) objects" << endl;
            } else if(flags & CQLDB_WR_SKIPPED) {
              //it is possible that within a batch of writes an update/delete/insert
              //may not get applied due to key conflicts etc. In such cases that
              //statement in the batch will be skipped but the rest of the batch
              //will still be applied.
              cout << debug_tag << debug_id << " write statement skipped" << endl;
            }
            if(flags & CQLDB_LEADER_TIMEDOUT) {
              //this messages needs to be printed only once, but we are lazy and
              //we print it for each statement in the batch
              cout << debug_tag << debug_id << " leader timedout waiting to aggreate WRITE query results" << endl;
            }
            if(flags & CQLDB_CLIENT_TIMEDOUT) {
              //this messages needs to be printed only once, but we are lazy and
              //we print it for each statement in the batch
              cout << debug_tag << debug_id << " client timedout waiting on WRITE query results" << endl;
            }
          } while(res.next_stmt());
        } catch(exception const &e) {
          //we will not terminate, the client we will keep moving on
          //should we possibly resubmit the query? Customer could track
          //the number of such queries to make sure there arent a lot
          cerr << "ERROR: " << e.what() << endl;
        }
      }

      //always reset current statment before moving onto the next
      stat.reset(); //release statement resources, else memory leaks/corruption

      //randomly decide if the select will be utilized in a batch.
      if(rand_r(&thread_seed)%2==0 && select_count < __NUM_SEL) {
        select_iter[select_count++] = iter;
      }

      if(i%1000==0) {
        /* notice that for stats queries within a read/write batch are counted individually
         * so a single batch of 4 selects is counted as 4 queries */
        cout << debug_tag << debug_id << " mixed RD iter#:" << i
             << " select:" << sel_stats
             << " range-select:" << range_selects
             << " WR-FIBERS:" << __NUM_WR_FIBERS
             << " RD-FIBERS:" << __NUM_RD_FIBERS
             << " no-results:" << no_results;
             //<< " num-flips:" << g_num_flips_performed;
        cqldb_session.packet_stats(&packets_sent, &packets_recvd, &packets_dropped);
        cout << " sent:" << packets_sent << " recvd:" << packets_recvd
             << " dropped:" << packets_dropped << endl;
      }

      if(select_count==__NUM_SEL) {
        int nxt, sel_batchsz;
        while(select_drain_count < select_count) {
          //choose a batch size and utilize it to lookup values
          sel_batchsz = rand_r(&thread_seed) % (select_count - select_drain_count);
          if(sel_batchsz==0) {
            sel_batchsz = 1;
          } else if(sel_batchsz>5) {
            sel_batchsz = 5;
          }
          std::string batchstr = "";
          for(j=0; j<sel_batchsz; j++) {
            if(j>0) {
              batchstr += ";";
            }
            batchstr += select_qstr;
          }
          //we are counting the batched select as a single select query
          sel_stats++;

          //dynamically constructed queries have to be created within the loop because
          //we can not prepare them in advance outside the loop and hence we incurr the
          //cost of repeatedly preparing the queries.
          cqldb::statement stat = cqldb_session << batchstr;

          //bind the select param using positional bind operator '<<'
          for(j=0; j<sel_batchsz; j++) {
            //NOTE::make certain that the parameters provided with query line up with
            //what is expected else you will get strange errors.
            tmp_iter = select_iter[select_drain_count+j];
            g_f1f2_table.compose_f1(client_id, wrfiber_id, tmp_iter, select_f1, __MAXLEN);
            g_f1f2_table.compose_f2(client_id, wrfiber_id, tmp_iter, select_f2, __MAXLEN);
            __COMPOSE_f4(select_f4, sizeof(select_f4));
            stat << select_f1 << select_f2 << __COMPOSE_f3(tmp_iter) << select_f4;
          }
          //optionally set timeout between (120, 255) seconds, increase if client
          //runs into query timeout, to avoid resubmitting the query
          stat.set_timeout(120);
          do {
            //each iteration in this loop resubmits request to cluster
            stat.exec();
            //should we add a tiny usleep() before resubmitting request?
          } while(!stat.got_response());

          try {
            cqldb::result res = stat.query();
            unsigned int flags;
            int num_sel_res=0;
            nxt=0;
            do {
              res.print_errors(flags);
              if(flags) {
                cout << debug_tag << debug_id << " errors were found in execution of statement:" << (nxt+1) <<endl;
                if(flags & CQLDB_HAS_DROPPED) {
                  cout << debug_tag << debug_id << " statement has some result packets dropped" << endl;
                }
                if(flags & CQLDB_HAS_TIMEOUT) {
                  cout << debug_tag << debug_id << " statement ran into timeout" << endl;
                }
                if(flags & CQLDB_HAS_LIMIT) {
                  cout << debug_tag << debug_id << " statement ran into record limit" << endl;
                }
                if(flags & CQLDB_LEADER_TIMEDOUT) {
                  cout << debug_tag << debug_id << " leader timedout waiting to aggreate results" << endl;
                }
                if(flags & CQLDB_CLIENT_TIMEDOUT) {
                  cout << debug_tag << debug_id << " client timedout waiting on results" << endl;
                }
                // CQLDB_HAS_PERPART_LIMIT is not yet supported, do not utilize in CQL
              }
              while(res.next()) {
                num_sel_res++;
              }
              if(!num_sel_res) {
                //cout << debug_tag << debug_id << " no results found for select" << endl;
                no_results++;
              }
              if(nxt==(sel_batchsz-1)) {
                break;
              }
              if(!res.next_stmt()) {
                cout << debug_tag << debug_id << " found fewer stmt results than expected" << endl;
              }
              nxt++;
            } while(1);
          } catch(exception const &e) {
            //we will not terminate, the client we will keep moving on
            //should we possibly resubmit the query? Customer could track
            //the number of such queries to make sure there arent a lot
            cerr << "ERROR: " << e.what() << endl;
          }
          stat.reset(); //release statement resources, else memory leaks/corruption
          select_drain_count += sel_batchsz;
        }
        select_drain_count=0;

        //we will also make individual selects (as opposed to the batch reads we just
        //made to increase the read load on the instances).
        for(j=0; j<select_count; j++) {

          sel_stats++;

          //this is a fixed query it can possibly be prepared before the loops
          //to prepare only once
          cqldb::statement stat = cqldb_session << select_qstr;

          //bind the select param using positional bind operator '<<'
          //NOTE::make certain that the parameters provided with query line up with
          //what is expected else you will get strange errors.
          tmp_iter = select_iter[j];
          g_f1f2_table.compose_f1(client_id, wrfiber_id, tmp_iter, select_f1, __MAXLEN);
          g_f1f2_table.compose_f2(client_id, wrfiber_id, tmp_iter, select_f2, __MAXLEN);
          __COMPOSE_f4(select_f4, sizeof(select_f4));
          stat << select_f1 << select_f2 << __COMPOSE_f3(tmp_iter) << select_f4;

          //optionally set timeout between (120, 255) seconds, increase if client
          //runs into query timeout, to avoid resubmitting the query
          stat.set_timeout(120);
          do {
            //each iteration in this loop resubmits request to cluster
            stat.exec();
            //should we add a tiny usleep() before resubmitting request?
          } while(!stat.got_response());

          try {
            cqldb::result res = stat.query();
            unsigned int flags;
            int num_sel_res=0;
            res.print_errors(flags);
            if(flags) {
              cout << debug_tag << debug_id << " errors were found in execution of statement:" << (nxt+1) <<endl;
              if(flags & CQLDB_HAS_DROPPED) {
                cout << debug_tag << debug_id << " statement has some result packets dropped" << endl;
              }
              if(flags & CQLDB_HAS_TIMEOUT) {
                cout << debug_tag << debug_id << " statement ran into timeout" << endl;
              }
              if(flags & CQLDB_HAS_LIMIT) {
                cout << debug_tag << debug_id << " statement ran into record limit" << endl;
              }
              if(flags & CQLDB_LEADER_TIMEDOUT) {
                cout << debug_tag << debug_id << " leader timedout waiting to aggreate results" << endl;
              }
              if(flags & CQLDB_CLIENT_TIMEDOUT) {
                cout << debug_tag << debug_id << " client timedout waiting on results" << endl;
              }
              // CQLDB_HAS_PERPART_LIMIT is not yet supported, do not utilize in CQL
            }
            while(res.next()) {
              num_sel_res++;
            }
            if(!num_sel_res) {
              //cout << debug_tag << debug_id << " no results found for select" << endl;
              no_results++;
            }
          } catch(exception const &e) {
            //we will not terminate, the client we will keep moving on
            //should we possibly resubmit the query? Customer could track
            //the number of such queries to make sure there arent a lot
            cerr << "ERROR: " << e.what() << endl;
          }
          stat.reset(); //release statement resources, else memory leaks/corruption
        }
        select_count=0;
      }

      if(test_range_quries && i>RANGE_TEST_THRESH) {
        //randomly decide if we are going to try a range-select we want to make
        //these queries somewhat infrequent, so that the overall test proceeds
        //at a fair rate
        int num_range_sel_res=0;
        if(iter && i%500==0) {

          //dynamically constructed queries have to be created within the loop because
          //we can not prepare them in advance outside the loop and hence we incurr the
          //cost of repeatedly preparing the queries.
          cqldb::statement stat = cqldb_session << select_range_qstr;

          chosen_index = (fast_rand32()) % iter;
          g_f1f2_table.compose_f1(client_id, wrfiber_id, chosen_index, f1, __MAXLEN);
          stat << f1;

          //optionally set a timeout of 120 seconds on the statement, this may be
          //needed for queries that may be taking longer to execute. comment it
          //out if its not needed. timeout has to be between (120, 255)
          stat.set_timeout(120);
          do {
            //each iteration in this loop resubmits request to cluster
            stat.exec();
            //should we add a tiny usleep() before resubmitting request?
          } while(!stat.got_response());

          try {
            cqldb::result res = stat.query();
            unsigned int flags;
            res.print_errors(flags);
            if(flags) {
              if(flags & CQLDB_HAS_LIMIT) {
                cout << debug_tag << debug_id << " range select ran into record limit" << endl;
              }
            }
            //enable this code if you want to print the results
            //while(res.next()) {
              //num_range_sel_res++;
            //}
            //cout << debug_tag << debug_id << " range select result count=" << num_range_sel_res << endl;
          } catch(exception const &e) {
            //we will not terminate, the client we will keep moving on
            //should we possibly resubmit the query? Customer could track
            //the number of such queries to make sure there arent a lot
            cerr << "ERROR: " << e.what() << endl;
          }
          stat.reset(); //release statement resources, else memory leaks/corruption
          range_selects++;
        }
      }
    }
    cout << debug_tag << debug_id << " SELECT #" << sel_stats;
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
    goto done;
  }
done:
  __LEAVE_RD_FIBER;
  return;
}

/*
 * launch_client_thread_fibers
 * launch the specified number of fibers on the current thread
 * thrdid - client thread id
 * num_fibers - number of fibers to launch
 * first_fiberid - fiberid of the first fiber
 */
static int
launch_client_thread_fibers(int client_id, int thrdid, int num_fibers, int first_fiberid)
{
  int rc=0, i;

  boost::fibers::fiber* all_fibers = new boost::fibers::fiber[num_fibers];

  //launch thread fibers
  for(i=0; i<num_fibers; i++) {
    client_thrd_arg_t arg;
    arg.client_id = client_id;
    arg.thrd_id = thrdid;
    arg.fiber_id = first_fiberid + i;
    arg.test_range_queries = 1;
    arg.larger_records = 1;
    //arg.larger_records = 0;

    if(i<(num_fibers>>1)) {
      all_fibers[i] = boost::fibers::fiber(custom_mixload_driver_thread_writer, arg);
    } else {
      all_fibers[i] = boost::fibers::fiber(custom_mixload_driver_thread_reader, arg);
    }
  }
  //join thread fibers
  for(i=0; i<num_fibers; i++) {
    all_fibers[i].join();
  }
err:
  return rc;
}

/*
 * launch_client_threads_with_fibers
 * launch client threads and fibers
 */
static int
launch_client_threads_with_fibers(int client_id)
{
  int rc=0, i;
  int max_threads, suggested_fibers, max_fibers, num, firstid, fiber_count;
  std::thread* all_threads;
  cpu_set_t cpuset;

  //NOTE:: max_threads, suggested_fibers are suggestions, depending upon the
  //observed performance of the client more threads and fibers can be created
  //
  cqldb::session::client_max_threads(&max_threads, &suggested_fibers, &max_fibers);
  //for virtual environment, for testing purposes only
  //max_threads = 1;
  suggested_fibers = 2045;

  if(suggested_fibers < max_threads) {
    rc = -1;
    goto err;
  }

  //NOTE:: fiber_count can be set some other value instead of suggested_fibers
  //so long as it is <= max_fibers
  //
  cout << "Launching Threads#:" << max_threads << " and fibers#:" << suggested_fibers << endl;
  num = suggested_fibers/max_threads;
  fiber_count = suggested_fibers;
  firstid = 0;

  all_threads = new std::thread[max_threads];

  g_f1f2_table.init(__DEF_F1F2_TBLSZ);
  __INIT_PROGRESS(fiber_count);
 
  //we will setup the minimal schema for all the tests. full_schema contains
  //a lot of secondary indexes and materialized views and is much slower and
  //is meant for the driver peachtest_driver_thread() to test functionality.
  //NOTE:: schema must be loaded only once before running any client threads
  __peachtest_setup_schema(0);

  //launch client threads, do NOT launch more threads than max_threads
  for(i=0; i<max_threads; i++) {
    assert(fiber_count);
    all_threads[i] = std::thread(launch_client_thread_fibers, client_id, i, num, firstid);
    num = (fiber_count>num)? num: (fiber_count);
    fiber_count -= num;
    firstid +=num;

    //distribute the threads uniformly amongst available vCPUs
    CPU_ZERO(&cpuset);
    CPU_SET(i, &cpuset);
    int rc = pthread_setaffinity_np(all_threads[i].native_handle(),
                                    sizeof(cpu_set_t), &cpuset);
    if(rc != 0) {
      std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
  }

  //join client threads
  for(i=0; i<max_threads; i++) {
    all_threads[i].join();
  }
err:
  return rc;
}

/*
 * launch_client_threads_without_fibers
 * launch client threads without fibers
 */
static int
launch_client_threads_without_fibers(int client_id)
{
  int rc=0, i;
  int max_threads, suggested_fibers, max_fibers, num;
  std::thread* all_threads;
  cpu_set_t cpuset;
  int cpu_count=0;

  //NOTE:: max_threads, suggested_fibers, are suggestions, depending upon the
  //observed performance of the client more threads and fibers can be created
  //
  cqldb::session::client_max_threads(&max_threads, &suggested_fibers, &max_fibers);

  //max_threads is set to the vCPU count in cloud instances
  cpu_count = max_threads;

  //NOTE:: max_threads can be set some other value instead of suggested_fibers
  //so long as it is <= max_fibers
  //
  //create threads as opposed to fibers
  max_threads = suggested_fibers;

  g_f1f2_table.init(__DEF_F1F2_TBLSZ);
  __INIT_PROGRESS(max_threads);

  all_threads = new std::thread[max_threads];
 
  //we will setup the minimal schema for all the tests. full_schema contains
  //a lot of secondary indexes and materialized views and is much slower and
  //is meant for the driver peachtest_driver_thread() to test functionality.
  //NOTE:: schema must be loaded only once before running any client threads
  __peachtest_setup_schema(0);

  //launch client threads
  num = max_threads;
  for(i=0; i<num; i++) {
    client_thrd_arg_t arg;
    arg.client_id = client_id;
    arg.thrd_id = i;
    arg.fiber_id = -1;
    arg.test_range_queries = 1;
    arg.larger_records = 1;
    //arg.larger_records = 0;

    if(i<(num>>1)) {
      all_threads[i] = std::thread(custom_mixload_driver_thread_writer, arg);
    } else {
      all_threads[i] = std::thread(custom_mixload_driver_thread_reader, arg);
    }

    //distribute the threads among available CPUs.
    CPU_ZERO(&cpuset);
    CPU_SET((i%cpu_count), &cpuset);
    int rc = pthread_setaffinity_np(all_threads[i].native_handle(),
                                    sizeof(cpu_set_t), &cpuset);
    if(rc != 0) {
      std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }
  }
  //join client threads
  for(i=0; i<max_threads; i++) {
    all_threads[i].join();
  }
err:
  return rc;
}

int
main(int argc, const char *argv[])
{
  int use_fibers = 1;

  /* NOTE:: the default is to utilize boost::fibers for the client, utilizing fibers
   * can allow possibly 24% higher throughput (with some measurements, your results
   * may differ). However utilizing fibers requires co-operative scheduling. You can
   * not make any blocking calls in the fibers and you have to explicitly invoke
   * boost::this_fiber::wait_for() or boost::this_fiber::yield() instead of blocking
   * calls to ensure that other fibers get a chance to execute. If you make blocking
   * i/o calls in fibers you will hang the thread running that fiber. For eg do not
   * utilize system calls in fibers else all fibers running on that OS thread will
   * be delayed till completion of the system call. If you do not want to deal with
   * co-operative scheduling then you can provide --no-fibers to this test and then
   * it will utilize only threads, in which you can make blocking calls. Fibers are
   * the preferable approach.
   */

  if(argc!=2 && argc!=3) {
usage:
    cerr << "Usage: " << argv[0] << " <unique_client_id in range [0,255]> [--no-fibers]" <<endl;
    cerr << "unique_client_id is a client_id that has not been utilized so far" << endl;
    cerr << "  on this or any other client" << endl;
    cerr << "--no-fibers - (optional) if provided, utilize threads instead of fibers" << endl;
    exit(-1);
  }
  int client_id = atoi(argv[1]);

  if(client_id<0 || client_id>255) {
    cerr << "client_id outside of range" << endl;
    goto usage;
  }
  if(argc==3) {
    if(strcmp("--no-fibers", argv[2])!=0) {
      cerr << "Unknown option:" << argv[2] << endl;
      goto usage;
    }
    use_fibers = 0;
  }
  /* Block SIGPIPE, so that any reads/writes don't terminate the client
   * but rather return EPIPE error, alternately the client can choose
   * to handle the signal with a signal handler
   */
  if(signal(SIGPIPE, SIG_IGN)==SIG_ERR) {
    cerr << "Unable to set ignore SIGPIPE" << endl;
    exit(-1);
  }

  if(use_fibers) {
    launch_client_threads_with_fibers(client_id);
  } else {
    launch_client_threads_without_fibers(client_id);
  }
  return 0; 
}
