/**
 * Copyright(c) 2021 PeachyDB inc
 *
 * peachydb_client_cqldb.cpp
 */

#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <sstream>
#include <thread>

#include <boost/fiber/all.hpp>

#include "peachdb_test.hpp"
#include "cqldb/cqldb_api.h"
#include "fast_rand.h"

//extern int g_num_flips_performed;


//packet stats can incur futex() system calls, so it might have a minor
//performance impact. In measurements it doesn't show up much at all,
//option is provided to disable it or enable it as needed.
//#define __PRINT_PACKET_STATS

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

static int g_num_active_fibers=0;

#define __START_FIBER (__sync_fetch_and_add(&g_num_active_fibers, 1))
#define __LEAVE_FIBER (__sync_fetch_and_sub(&g_num_active_fibers, 1))
#define __NUM_FIBERS  (__sync_fetch_and_add(&g_num_active_fibers, 0))

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
    stat.set_timeout(120);

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
 * peachdbtest_setup_schema
 * setup the schema, security data, initial data
 * NOTE:: if full_schema==1 then all indexes and materialized views will be
 * created, which will slow down the write performance by a lot. For a lot
 * of usages we should be utilizing full_schema==0.  (for client driver
 * peachtest_driver_thread() we can utilize full schema to test functionality).
 * NOTE:: this function is not thread safe. it should be invoked to load
 * schema into the database before any client threads are launched.
 */
static int
peachtest_setup_schema(int full_schema)
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
 * custom_driver_thread
 * primarily meant for insert performance testing
 */
static void
custom_driver_thread(client_thrd_arg_t arg)
{
#define __MAX_FLDLEN (20)
#define __NLKUPS 100
  int len, i, j;
  char f1[__MAX_FLDLEN], f2[__MAX_FLDLEN], f4[__MAX_FLDLEN];
  char ip1[__MAX_FLDLEN], ip2[__MAX_FLDLEN];
#define __MAX_DESC 512 //maximum length of description field
  char descfield[__MAX_DESC];
  char lookup[__NLKUPS][__MAX_FLDLEN];
  int client_id = arg.client_id;
  int fiber_id = arg.fiber_id;
  int thrd_id = arg.thrd_id;
  const char* debug_tag = (fiber_id==-1)? g_thread_tag: g_fiber_tag;
  int debug_id = (fiber_id==-1)? thrd_id: fiber_id;
  uint64_t packets_sent, packets_recvd, packets_dropped;
  unsigned int thread_seed;

  const char* insert_qstr = "INSERT INTO testkeyspace.test_table1 (field1, field2, field3, field4, field5, field6, description, listfield, setfield, mapfield) VALUES (:fld1, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  const char* update_qstr = "UPDATE testkeyspace.test_table1 SET listfield = ['todo', 'when'] + listfield WHERE field1 =? AND field2 =?";
  const char* delete_qstr = "DELETE FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =?";
  const char* select_qstr = "SELECT field1,field2,field3,field4,field5,field6,listfield,setfield,mapfield FROM testkeyspace.test_table1 WHERE field1 = ?";

  //per thread initialization
  fast_randseed();
  //utilize thread_seed for rand_r() instead of rand() to avoid futex calls
  thread_seed = time(NULL) ^ (debug_id << 16);
  __START_FIBER;

  try {
    //session must be created before invoking the driver
    ostringstream session_params;
    session_params << "peachdb_cluster:"
                   << "cluster-configuration-path=" << cluster_configuration_path << ";"
                   << "self-configuration-path=" << self_configuration_path;

    //setup a clustered peachdb session, NOTE: session handles are not
    //thread safe, nor are any of the statement or any other handles.
    cqldb::session cqldb_session = cqldb::session(session_params.str());

    //statements that are fixed, with only arguments/params changing across
    //invocation can be prepared once before the execution loop and then be
    //repeatedly reset and provided new parameters. This can not be done if
    //the statement is a batch statement with varying numbers and kinds of
    //queries or cases where the query is not known in advance. Preparing the
    //queries once has been shown to improve CPU utilization by 25% or more.
    //
    //BUG:: In some corner cases it is possible that preparing statements
    //outside the outermost for() loop below as well as within the loop can
    //lead to memory leak (ie preparing in both the places together, preparing
    //in only one spot is not an issue). If you observe this please report it.
    //
    cqldb::statement insert_stat = cqldb_session << insert_qstr;

    //cqldb::statement update_stat = cqldb_session << update_qstr;
    //cqldb::statement delete_stat = cqldb_session << delete_qstr;
    cqldb::statement select_stat = cqldb_session << select_qstr;

    cout << debug_tag << debug_id << " kicking off data load" << endl;
    for(i=0; i<100000000; i++) {

      len = 7 + rand_r(&thread_seed)%10;
      /* utilize client_id, fiber_id to ensure different clients,threads generate
       * non-over lapping keys */
      f1[0] = (client_id & 0x0f) + 'a';
      f1[1] = (client_id>>4) + 'a';

      //turn f1[2] and f1[3] into characters 'a'-'z'
      f1[2] = ((fiber_id==-1)? thrd_id:fiber_id);
      f1[3] = (f1[2]>>4) + 'a';
      f1[2] = (f1[2] & 0x0f) + 'a';

      for(j=4; j<len; j++) {
        f1[j] = 'a' + rand_r(&thread_seed)%26;
      }
      f1[j] = '\0';

      if(i < __NLKUPS) {
        lookup[i][0] = '\0';
        sprintf(&lookup[i][0], "%s", f1);
      }

      len = 7 + rand_r(&thread_seed)%10;
      for(j=0; j<len; j++) {
        f2[j] = 'a' + rand_r(&thread_seed)%26;
      }
      f2[j] = '\0';
      snprintf(f4, sizeof(f4), "10:01:34");
      snprintf(ip1, sizeof(ip1), "%d.%d.%d.%d",
               123, (((i>>16)+1)& 0xff), ((i>>8) & 0xff), (i & 0xff));
      snprintf(ip2, sizeof(ip2), "%d.%d.%d.%d",
               123, (((i>>16)+2)& 0xff), ((i>>8) & 0xff), (i & 0xff));

      len = 1;
      if(arg.larger_records) {
        len = (fast_rand32() % __MAX_DESC) - 1;
        if(len < 256) {
          len = 256; //since we are attempting larger records utilize atleast 128 bytes.
        }
      }
      for(j=0; j<len; j++) {
        //caution::synthetic field may be artificially more compressible, if
        //compression is utilized.
        descfield[j] = 'a' + (uint8_t)(fast_rand32() & 0xf);
      }
      descfield[j] = '\0';

      //NOTE::make certain that the parameters provided with query line up with
      //what is expected else you will get strange errors.
      cqldb::user_collection listf = insert_stat.create_user_collection();
      listf << f1 << f2;
      cqldb::user_collection setf = insert_stat.create_user_collection();
      setf << f1 << f2;
      cqldb::user_collection mapf = insert_stat.create_user_collection();
      mapf << "key1" << f1 << "key2" << f2;

      //a concise syntax to execute the query
      //make sure to bind the parameters in correct order and count else you get
      //parameter binding errors, syntax errors etc.
      insert_stat.bind("fld1", f1);
      insert_stat << f2 << (i+100) << f4 << ip1 <<ip2;
      insert_stat << descfield << listf << setf << mapf;
      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      insert_stat.set_timeout(120);
      do {
        //each iteration in this loop resubmits request to cluster
        insert_stat.exec();
      } while(!insert_stat.got_response() && __check_resubmit_write(insert_stat));
      insert_stat.reset(); //release statement resources, else memory leaks/corruption

      /******
        alternate syntax to achieve the same
        make sure to bind the parameters in correct order and count else you get
        parameter binding errors, syntax errors etc.
        insert_stat.bind("fld1", f1);
        insert_stat.bind(f2);
        insert_stat.bind(i+100);
        insert_stat.bind(f4);
        insert_stat.bind(ip1);
        insert_stat.bind(ip2);
        insert_stat.bind(descfield);
        insert_stat.bind(listf);
        insert_stat.bind(setf);
        insert_stat.bind(mapf);
        //optionally set timeout between (120, 255) seconds, increase if client
        //runs into query timeout, to avoid resubmitting the query
        insert_stat.set_timeout(120);
        do {
          //each iteration in this loop resubmits request to cluster
          insert_stat.exec();
        } while(!insert_stat.got_response() && __check_resubmit_write(insert_stat));
        insert_stat.reset(); //release statement resources, else memory leaks/corruption
      *******/
      /****
        alternate syntax to achieve the same, this syntax can be utilized
        in such cases where user_collection is not involved. user_collection
        is currently created wrt a statement. Perhaps we should change this
        cqldb::result res = cqldb_session << insert_qstr << f1 << f2 << (i+100) << f4
                            << ip1 << ip2 << descfield << listf << setf << mapf;
      ****/

      /**********
      //perform udpate query
      update_stat.bind(f1);
      update_stat.bind(f2);
      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      update_stat.set_timeout(120);
      do {
        //each iteration in this loop resubmits request to cluster
        update_stat.exec();
      } while(!update_stat.got_response() && __check_resubmit_write(update_stat));
      update_stat.reset(); //release statement resources, else memory leaks/corruption

      //perform delete query
      delete_stat.bind(f1);
      delete_stat.bind(f2);
      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      delete_stat.set_timeout(120);
      do {
        //each iteration in this loop resubmits request to cluster
        delete_stat.exec();
      } while(!delete_stat.got_response() && __check_resubmit_write(delete_stat));
      delete_stat.reset(); //release statement resources, else memory leaks/corruption
      *******/

      if(i%1000==0) {
        cout << debug_tag << debug_id << " iter data load: " << i
             << " num-fibers:" << __NUM_FIBERS;

#ifdef __PRINT_PACKET_STATS
        if(i%4096==0) {
          //print out packets stats even less often, as they can incurr system calls
          cqldb_session.packet_stats(&packets_sent, &packets_recvd, &packets_dropped);
          cout << " sent:" << packets_sent << " recvd:" << packets_recvd
               << " dropped:" << packets_dropped << endl;
        } else {
          cout << endl;
        }
#else
        cout << endl;
#endif
      }
    }
    cout << debug_tag << debug_id << " done with data load" << endl;

    cout << debug_tag << debug_id << " lookup inserted data" << endl;
    /* NOTE:: to extract results with >> operator syntax, you have to list all the
     * fields, and extract them in order, if the query is 'SELECT * ...' then 
     * order of fields is not pre-determined */
    for(i=0; i< __NLKUPS; i++) {
      select_stat.bind(lookup[i]);
      cout << debug_tag << debug_id << " about to execute " << select_qstr << endl;

      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      select_stat.set_timeout(120);
      do {
        //each iteration in this loop resubmits request to cluster
        select_stat.exec();
        //should we add a tiny usleep() before resubmitting request?
      } while(!select_stat.got_response());

      cout << debug_tag << debug_id << " got resp from leader" << endl;
      try {
        cqldb::result res = select_stat.query();
        if(!res.next()) {
          cout << debug_tag << debug_id << " no resp found" << endl;
          continue;
        }
        if(res.is_transactional()) {
          cout << debug_tag << debug_id << " query results are transactional" << endl;
        } else {
          cout << debug_tag << debug_id << " query results are non-transactional" << endl;
        }
        unsigned int flags;
        res.print_errors(flags);
        if(flags) {
          cout << debug_tag << debug_id << " some errors were found in query execution" << endl;
        }
        cout << debug_tag << debug_id << " printing response" << endl;
        std::string field1, field2, field4, field5, field6;
        cqldb::user_collection rescoll = select_stat.create_user_collection();
        std::string key, val;
        int field3;

        res >> field1 >> field2 >> field3
            >> field4 >> field5 >> field6;
        cout << "field1 = " << field1;
        cout << ", field2 = " << field2;
        cout << ", field3 = " << field3;
        cout << ", field4 = " << field4;
        cout << ", field5 = " << field5;
        cout << ", field6 = " << field6;

        res >> rescoll;
        len = rescoll.length();
        cout << ", listfield = [";
        for(j=0; j<len; j++) {
          rescoll >> val;
          cout << (j>0? ", ": "\'") << val << "\'";
        }
        cout << "]";

        res >> rescoll;
        len = rescoll.length();
        cout << ", setfield = {";
        for(j=0; j<len; j++) {
          rescoll >> val;
          cout << (j>0? ", ": "\'") << val << "\'";
        }
        cout << "}";

        res >> rescoll;
        len = rescoll.length();
        cout << ", mapfield = {";
        for(j=0; j<len; j+=2) {
          rescoll >> key;
          rescoll >> val;
          if(j>0) {
            cout << ", ";
          }
          cout << "\'" << key << "\': \'" << val << "\'";
        }
        cout << "}" << endl;
      } catch(exception const &e) {
        //we will not terminate, the client we will keep moving on
        //should we possibly resubmit the query? Customer could track
        //the number of such queries to make sure there arent a lot
        cerr << "ERROR: " << e.what() << endl;
      }

      /***********
       alternate syntax to achieve the same as above
       if the query were expressed as below, then we would have to utilize .fetch()
       interface to extract fields for each result tuple, because the order of fields
       returned is not determinate
       const char* select_qstr = "SELECT * FROM testkeyspace.test_table1 WHERE field1 = ?";
       cqldb::statement select_stat = cqldb_session << select_qstr;
       //optionally set timeout between (120, 255) seconds, increase if client
       //runs into query timeout, to avoid resubmitting the query
       select_stat.set_timeout(120);
       do {
        //each iteration in this loop resubmits request to cluster
         select_stat.exec();
        //should we add a tiny usleep() before resubmitting request?
       } while(!select_stat.got_response());

       try {
        cqldb::result res = select_stat.query();
        res.fetch("field1", field1);
        cout << "field1 = " << field1;
        res.fetch("field2", field2);
        cout << ", field2 = " << field2;
        res.fetch("field3", field3);
        cout << ", field3 = " << field3;
        res.fetch("field4", field4);
        cout << ", field4 = " << field4;
        res.fetch("field5", field5);
        cout << ", field5 = " << field5;
        res.fetch("field6", field6);
        cout << ", field6 = " << field6;
        cqldb::user_collection rescoll = select_stat.create_user_collection();
        res.fetch("listfield", rescoll);
        cout << ", listfield = [";
        len = rescoll.length();
        for(j=0; j<len; j++) {
          rescoll >> val;
          cout << (j>0? ", ": "\'") << val << "\'";
        }
        cout << "]";
        res.fetch("setfield", rescoll);
        len = rescoll.length();
        cout << ", setfield = {";
        for(j=0; j<len; j++) {
          rescoll >> val;
          cout << (j>0? ", ": "\'") << val << "\'";
        }
        cout << "}";
        res.fetch("mapfield", rescoll);
        len = rescoll.length();
        cout << ", mapfield = {";
        for(j=0; j<len; j+=2) {
          rescoll >> key;
          rescoll >> val;
          if(j>0) {
            cout << ", ";
          }
          cout << "\'" << key << "\': \'" << val << "\'";
        }
        cout << "}" << endl;
      } catch(exception const &e) {
        cerr << "ERROR: " << e.what() << endl;
      }
      ********/

      select_stat.reset(); //release statement resources, else memory leaks/corruption
    }
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
    goto done;
  }
done:
  __LEAVE_FIBER;
  return;
}

/*
 * custom_update_driver_thread
 * this driver loads 100k items and then only repeatedly updates __NUPDATES items
 * from it. This is meant to test the impact of virtual memory page faults. If the
 * same items are being updated repeatedly then page faults will not occur and
 * we will be able to observe the bevaiour of the product without page faults.
 */
static void
custom_update_driver_thread(client_thrd_arg_t arg)
{
#define __MAX_FLDLEN (20)
  int len, i, j;
  char f1[__MAX_FLDLEN], f2[__MAX_FLDLEN], f4[__MAX_FLDLEN];
  char ip1[__MAX_FLDLEN], ip2[__MAX_FLDLEN];
#define __MAX_DESC 512 //maximum length of description field
  char descfield[__MAX_DESC];
#define __NUPDATES  1000
  char update_f1[__NUPDATES][__MAX_FLDLEN];
  char update_f2[__NUPDATES][__MAX_FLDLEN];
  int updnum=0;
  int client_id = arg.client_id;
  int fiber_id = arg.fiber_id;
  int thrd_id = arg.thrd_id;
  const char* debug_tag = (fiber_id==-1)? g_thread_tag: g_fiber_tag;
  int debug_id = (fiber_id==-1)? thrd_id: fiber_id;
  uint64_t packets_sent, packets_recvd, packets_dropped;
  unsigned int thread_seed;

  const char* insert_qstr = "INSERT INTO testkeyspace.test_table1 (field1, field2, field3, field4, field5, field6, description, listfield, setfield, mapfield) VALUES (:fld1, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  const char* even_update_qstr = "UPDATE testkeyspace.test_table1 SET listfield = ['todo', 'when'] WHERE field1 =? AND field2 =?";
  const char* odd_update_qstr = "UPDATE testkeyspace.test_table1 SET listfield = ['todo', 'when', 'we'] WHERE field1 =? AND field2 =?";
  const char* delete_qstr = "DELETE FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =?";

  //per thread initialization
  fast_randseed();
  //utilize thread_seed for rand_r() instead of rand() to avoid futex calls
  thread_seed = time(NULL) ^ (debug_id << 16);
  __START_FIBER;

  try {
    //session must be created before invoking the driver
    ostringstream session_params;
    session_params << "peachdb_cluster:"
                   << "cluster-configuration-path=" << cluster_configuration_path << ";"
                   << "self-configuration-path=" << self_configuration_path;

    //setup a clustered peachdb session, NOTE: session handles are not
    //thread safe, nor are any of the statement or any other handles.
    cqldb::session cqldb_session = cqldb::session(session_params.str());

    //statements that are fixed, with only arguments/params changing across
    //invocation can be prepared once before the execution loop and then be
    //repeatedly reset and provided new parameters. This can not be done if
    //the statement is a batch statement with varying numbers and kinds of
    //queries or cases where the query is not known in advance. Preparing the
    //queries once has been shown to improve CPU utilization by 25% or more.
    //
    //BUG:: In some corner cases it is possible that preparing statements
    //outside the outermost for() loop below as well as within the loop can
    //lead to memory leak (ie preparing in both the places together, preparing
    //in only one spot is not an issue). If you observe this please report it.
    //
    cqldb::statement insert_stat = cqldb_session << insert_qstr;
    cqldb::statement odd_update_stat = cqldb_session << odd_update_qstr;
    cqldb::statement even_update_stat = cqldb_session << even_update_qstr;

    cout << debug_tag << debug_id << " kicking off data load" << endl;

#define  TOTAL_LOAD_UPDTEST  100000000

    for(i=0; i<TOTAL_LOAD_UPDTEST; i++) {
      len = 7 + rand_r(&thread_seed)%10;
      /* utilize client_id, fiber_id to ensure different clients,threads generate
       * non-over lapping keys */

      f1[0] = (client_id & 0x0f) + 'a';
      f1[1] = (client_id>>4) + 'a';

      //turn f1[2] and f1[3] into characters 'a'-'z'
      f1[2] = ((fiber_id==-1)? thrd_id:fiber_id);
      f1[3] = (f1[2]>>4) + 'a';
      f1[2] = (f1[2] & 0x0f) + 'a';

      for(j=4; j<len; j++) {
        f1[j] = 'a' + rand_r(&thread_seed)%26;
      }
      f1[j] = '\0';

      len = 5 + rand_r(&thread_seed)%10;
      for(j=0; j<len; j++) {
        f2[j] = 'a' + rand_r(&thread_seed)%26;
      }
      f2[j] = '\0';

      //remember f1,f2 for purpose of updates
      if((i % (TOTAL_LOAD_UPDTEST/__NUPDATES))==0) {
        updnum = i / (TOTAL_LOAD_UPDTEST/__NUPDATES);
        update_f1[updnum][0] = '\0';
        sprintf(&update_f1[updnum][0], "%s", f1);
        update_f2[updnum][0] = '\0';
        sprintf(&update_f2[updnum][0], "%s", f2);
      }

      snprintf(f4, sizeof(f4), "10:01:34");
      snprintf(ip1, sizeof(ip1), "%d.%d.%d.%d",
               123, (((i>>16)+1)& 0xff), ((i>>8) & 0xff), (i & 0xff));
      snprintf(ip2, sizeof(ip2), "%d.%d.%d.%d",
               123, (((i>>16)+2)& 0xff), ((i>>8) & 0xff), (i & 0xff));

      len = 1;
      if(arg.larger_records) {
        len = (fast_rand32() % __MAX_DESC) - 1;
        if(len < 256) {
          len = 256; //since we are attempting larger records utilize atleast 128 bytes.
        }
      }
      for(j=0; j<len; j++) {
        //caution::synthetic field may be artificially more compressible, if
        //compression is utilized.
        descfield[j] = 'a' + (uint8_t)(fast_rand32() & 0xf);
      }
      descfield[j] = '\0';

      cqldb::user_collection listf = insert_stat.create_user_collection();
      listf << f1 << f2;
      cqldb::user_collection setf = insert_stat.create_user_collection();
      setf << f1 << f2;
      cqldb::user_collection mapf = insert_stat.create_user_collection();
      mapf << "key1" << f1 << "key2" << f2;

      //a concise syntax to execute the query
      //make sure to bind the parameters in correct order and count else you get
      //parameter binding errors, syntax errors etc.
      insert_stat.bind("fld1", f1);
      insert_stat << f2 << (i+100) << f4 << ip1 <<ip2;
      insert_stat << descfield << listf << setf << mapf;

      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      insert_stat.set_timeout(120);
      do {
        //each iteration in this loop resubmits request to cluster
        insert_stat.exec();
      } while(!insert_stat.got_response() && __check_resubmit_write(insert_stat));

      insert_stat.reset(); //release statement resources, else memory leaks/corruption

      if(i%1000==0) {
        cout << debug_tag << debug_id << " iter update driver: " << i 
             << " num-fibers:" << __NUM_FIBERS;
#ifdef __PRINT_PACKET_STATS
        if(i%4096==0) {
          //print out packets stats even less often, as they can incurr system calls
          cqldb_session.packet_stats(&packets_sent, &packets_recvd, &packets_dropped);
          cout << " sent:" << packets_sent << " recvd:" << packets_recvd
               << " dropped:" << packets_dropped << endl;
        } else {
          cout << endl;
        }
#else
        cout << endl;
#endif
      }
    }
    cout << debug_tag << debug_id << " done with data load" << endl;

#define  TOTAL_UPDATES (2000000)

    cout << debug_tag << debug_id << " UPDATE inserted data" << endl;
    for(i=0; i< TOTAL_UPDATES/__NUPDATES; i++) {
      for(j=0; j< __NUPDATES; j++) {
        cqldb::statement* updstat = ((j&0x01) ? &odd_update_stat: &even_update_stat);
        updstat->bind(update_f1[j]);
        updstat->bind(update_f2[j]);
        //optionally set timeout between (120, 255) seconds, increase if client
        //runs into query timeout, to avoid resubmitting the query
        updstat->set_timeout(120);
        do {
          //each iteration in this loop resubmits request to cluster
          updstat->exec();
        } while(!updstat->got_response() && __check_resubmit_write(*updstat));

        updstat->reset(); //release statement resources, else memory leaks/corruption
      }
      cout << debug_tag << debug_id << " updated items iter: " << i << endl;
    }
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
    goto done;
  }
done:
  __LEAVE_FIBER;
  return;
}


/*
 * custom_large_requests_driver_thread
 * tries a mix of queries with a good number of requests
 * in each batch
 * - batch writes with a good number of requests
 * - batch reads with a good number of reads
 */
static void
custom_large_requests_driver_thread(client_thrd_arg_t arg)
{
  int len, i, j;
  int chosen_index=0;
  int client_id = arg.client_id;
  int fiber_id = arg.fiber_id;
  int thrd_id = arg.thrd_id;
  const char* debug_tag = (fiber_id==-1)? g_thread_tag: g_fiber_tag;
  int debug_id = (fiber_id==-1)? thrd_id: fiber_id;
  uint64_t packets_sent, packets_recvd, packets_dropped;
  unsigned int thread_seed;

#define __MAXLEN  (20)
#define __NUM_SEL (20)
  char f1[__MAXLEN], f2[__MAXLEN], f4[__MAXLEN];
  char ip1[__MAXLEN], ip2[__MAXLEN];
#define __MAX_DESC 512 //maximum length of description field
  char descfield[__MAX_DESC];

  char select_f1[__NUM_SEL][__MAXLEN];
  char select_f2[__NUM_SEL][__MAXLEN];
  int  select_count=0, select_iter=0;
  int  select_drain_count=0;
  int sel_stats=0;

  const char* insert_qstr = "INSERT INTO testkeyspace.test_table1 (field1, field2, field3, field4, field5, field6, description, listfield, setfield, mapfield) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

  const char* select_qstr = "SELECT field1,field2,field3,field4,field5,field6,listfield,setfield,mapfield FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =?";

  //test select without partition key provided but with limit provided
  const char* select_qstr_limit = "SELECT field1,field2,field3,field4,field5,field6,listfield,setfield,mapfield FROM testkeyspace.test_table1 WHERE field1 =? LIMIT 50";

  //per thread initialization
  fast_randseed();
  //utilize thread_seed for rand_r() instead of rand() to avoid futex calls
  thread_seed = time(NULL) ^ (debug_id << 16);
  __START_FIBER;

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

    std::string write_batchstr = "";
    int write_batchsz=0;
    int count;

    for(i=0; i<100000000; i++) {
      write_batchsz = rand_r(&thread_seed) % 40;
      count=0;

      if(write_batchsz < 10) {
        write_batchsz = 10;
      }
      //cout << debug_tag << debug_id << " mixed RW/RD batchsz:" << write_batchsz << endl;

      write_batchstr = insert_qstr;
      while(++count < write_batchsz) {
        write_batchstr = (write_batchstr + ";") +  insert_qstr;
      }

      //dynamically constructed queries have to be created within the loop because
      //we can not prepare them in advance outside the loop and hence we incurr the
      //cost of repeatedly preparing the queries.
      //
      //BUG:: In some corner cases it is possible that preparing statements
      //outside the outermost for() loop below as well as within the loop can
      //lead to memory leak (ie preparing in both the places together, preparing
      //in only one spot is not an issue). If you observe this please report it.
      //
      cqldb::statement stat = cqldb_session << write_batchstr;

      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      stat.set_timeout(120); //example of how to set timeout on query

      while(count--) {
        len = 7 + rand_r(&thread_seed)%10;
        /* utilize client_id, fiber_id to ensure different clients,threads generate
         * non-over lapping keys */
        f1[0] = (client_id & 0x0f) + 'a';
        f1[1] = (client_id>>4) + 'a';

        //turn f1[2] and f1[3] into characters 'a'-'z'
        f1[2] = ((fiber_id==-1)? thrd_id:fiber_id);
        f1[3] = (f1[2]>>4) + 'a';
        f1[2] = (f1[2] & 0x0f) + 'a';

        for(j=4; j<len; j++) {
          f1[j] = 'a' + rand_r(&thread_seed)%26;
        }
        f1[j] = '\0';

        len = 5 + rand_r(&thread_seed)%10;
        for(j=0; j<len; j++) {
          f2[j] = 'a' + rand_r(&thread_seed)%26;
        }
        f2[j] = '\0';

        snprintf(f4, sizeof(f4), "10:01:34");
        snprintf(ip1, sizeof(ip1), "%d.%d.%d.%d",
                 123, (((i>>16)+1)& 0xff), ((i>>8) & 0xff), (i & 0xff));
        snprintf(ip2, sizeof(ip2), "%d.%d.%d.%d",
                 123, (((i>>16)+2)& 0xff), ((i>>8) & 0xff), (i & 0xff));

        len = 1;
        if(arg.larger_records) {
          len = (fast_rand32() % __MAX_DESC) - 1;
          if(len < 256) {
            len = 256; //since we are attempting larger records utilize atleast 128 bytes.
          }
        }
        for(j=0; j<len; j++) {
          //caution::synthetic field may be artificially more compressible, if
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
        stat << f1 << f2 << (i+100) << f4 << ip1 <<ip2;
        stat << descfield << listf << setf << mapf;

        /* randomly decide if the value is going to be looked up */
        if(rand_r(&thread_seed)%2==0 && select_count < __NUM_SEL) {
          select_f1[select_count][0] = '\0';
          sprintf(&select_f1[select_count][0], "%s", f1);
          select_f2[select_count][0] = '\0';
          sprintf(&select_f2[select_count][0], "%s", f2);
          select_count++;
        }
        i++;
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
          int counter=0;
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
              cout << debug_tag << debug_id << " write statement: " << counter << " skipped" << endl;
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
            counter++;
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
        /* notice that for stats queries within a read/write batch are counted individually
         * so a single batch of 4 selects is counted as 4 queries */
        cout << debug_tag << debug_id << " mixed RW/RD iter#:" << i
             << " select:" << sel_stats << " num-fibers:" << __NUM_FIBERS;
#ifdef __PRINT_PACKET_STATS
        if(i%4096==0) {
          //print out packets stats even less often, as they can incurr system calls
          cqldb_session.packet_stats(&packets_sent, &packets_recvd, &packets_dropped);
          cout << " sent:" << packets_sent << " recvd:" << packets_recvd
               << " dropped:" << packets_dropped << endl;
        } else {
          cout << endl;
        }
#else
        cout << endl;
#endif
      }

      if(select_count==__NUM_SEL) {
        int nxt, sel_batchsz;
        if(select_iter==0) {
          select_iter = i;
          continue;
        }
#define __WAIT_AWHILE (400)
        if(select_iter + __WAIT_AWHILE > i) {
          continue;
        }
        int utilize_limit;
        while(select_drain_count < select_count) {
          //choose a batch size and utilize it to lookup values
          utilize_limit=0;
          if(rand_r(&thread_seed)%5==0) {
            utilize_limit=1;
          }

          sel_batchsz = rand_r(&thread_seed) % (select_count - select_drain_count);
          if(sel_batchsz==0) {
            sel_batchsz = 10;
          } else if(sel_batchsz > 16) {
            sel_batchsz = 16;
          }
          std::string batchstr = "";
          for(j=0; j<sel_batchsz; j++) {
            if(j>0) {
              batchstr += ";";
            }
            if((j+1==sel_batchsz) && (utilize_limit)) {
              batchstr += select_qstr_limit;
            } else {
              batchstr += select_qstr;
            }
            sel_stats++;
          }

          //dynamically constructed queries have to be created within the loop because
          //we can not prepare them in advance outside the loop and hence we incurr the
          //cost of repeatedly preparing the queries.
          cqldb::statement stat = cqldb_session << batchstr;

          //bind the select param using positional bind operator '<<'
          for(j=0; j<sel_batchsz; j++) {
            //NOTE::make certain that the parameters provided with query line up with
            //what is expected else you will get strange errors.
            if((j+1==sel_batchsz) && (utilize_limit)) {
              stat << select_f1[select_drain_count+j];
            } else {
              stat << select_f1[select_drain_count+j];
              stat << select_f2[select_drain_count+j];
            }
          }
          //optionally set timeout between (120, 255) seconds, increase if client
          //runs into query timeout, to avoid resubmitting the query
          stat.set_timeout(120); //example of how to set timeout on query
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
                cout << debug_tag << debug_id << " no results found for select" << endl;
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
        select_count=0;
        select_drain_count=0;
        select_iter=0;
      }
    }
    cout << debug_tag << debug_id << " SELECT #" << sel_stats;
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
    goto done;
  }
done:
  __LEAVE_FIBER;
  return;
}

/*
 * custom_mixload_driver_thread
 * tries a mix of queries
 * - single writes
 * - single reads
 * - batch writes
 * - batch reads
 */
static void
custom_mixload_driver_thread(client_thrd_arg_t arg)
{
  int len, i, j;
  int chosen_index=0;
  int client_id = arg.client_id;
  int fiber_id = arg.fiber_id;
  int thrd_id = arg.thrd_id;
  int test_range_quries = arg.test_range_queries;
  const char* debug_tag = (fiber_id==-1)? g_thread_tag: g_fiber_tag;
  int debug_id = (fiber_id==-1)? thrd_id: fiber_id;
  uint64_t packets_sent, packets_recvd, packets_dropped;
  unsigned int thread_seed;

#define __MAXLEN  (20)
#define __NUM_UPD (12)
#define __NUM_DEL (12)
#define __NUM_SEL (20)
  char f1[__MAXLEN], f2[__MAXLEN], f4[__MAXLEN];
  char ip1[__MAXLEN], ip2[__MAXLEN];
#define __MAX_DESC 512 //maximum length of description field
  char descfield[__MAX_DESC];

  char delete_f1[__NUM_DEL][__MAXLEN];
  char delete_f2[__NUM_DEL][__MAXLEN];
  int  delete_f3[__NUM_DEL];
  char delete_f4[__NUM_DEL][__MAXLEN];
  int  delete_count=0;
  int  delete_drain_count=0;

  char update_f1[__NUM_UPD][__MAXLEN];
  char update_f2[__NUM_UPD][__MAXLEN];
  int  update_f3[__NUM_UPD];
  char update_f4[__NUM_UPD][__MAXLEN];
  int  update_count=0;
  int  update_drain_count=0;

  char select_f1[__NUM_SEL][__MAXLEN];
  char select_f2[__NUM_SEL][__MAXLEN];
  int  select_count=0;
  int  select_drain_count=0;
  int  looked_up;
  int del_stats=0, upd_stats=0, sel_stats=0;

#define DISTINCT_VALS_THRESH (200000)
  char** range_test_f1=NULL;
  char** range_test_f2=NULL;
  int range_selects=0, range_deletes=0;

  const char* insert_qstr = "INSERT INTO testkeyspace.test_table1 (field1, field2, field3, field4, field5, field6, description, listfield, setfield, mapfield) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

  //NOTE::updates and deletes that are part of a batch must restrict all primary key
  //columns with equality constraints. (updates, deletes that modify multiple
  //objects can not be part of a batched transaction).
  const char* update_qstr = "UPDATE testkeyspace.test_table1 SET listfield = ['todo', 'when'] + listfield WHERE field1 =? AND field2 =? AND field3 =? AND field4 =?";
  const char* delete_qstr = "DELETE FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =? AND field3 =? AND field4 =?";

  const char* select_qstr = "SELECT field1,field2,field3,field4,field5,field6,listfield,setfield,mapfield FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =?";

  //if testing range queries is enabled then we will check range delete and range select
  //for range deletes all partition key fields of base table as well as materialized views 
  //must be restricted.
  const char* delete_range_qstr = "DELETE FROM testkeyspace.test_table1 WHERE field1 =? AND field2 =?";

  const char* select_range_qstr = "SELECT field1,field2,field3,field4,field5,field6,listfield,setfield,mapfield FROM testkeyspace.test_table1 WHERE field1 =? LIMIT 200";

  //per thread initialization
  fast_randseed();
  //utilize thread_seed for rand_r() instead of rand() to avoid futex calls
  thread_seed = time(NULL) ^ (debug_id << 16);
  __START_FIBER;

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

    if(test_range_quries) {
      /* IMPORTANT:: when utilizing a very large number of fibers, this
       * malloc() can give the appearence of a memory leak, in such cases
       * you can reduce the number of fibers that invoke this feature (by
       * adding code offcourse), or comment this feature out
       */

      /* for range select/delete test we will record the distinct f1 values we
       * are creating in a table and utilize these for range selects */
      range_test_f1 = (char**)malloc(DISTINCT_VALS_THRESH * sizeof(char*));
      range_test_f2 = (char**)malloc(DISTINCT_VALS_THRESH * sizeof(char*));
    }

    for(i=0; i<100000000; i++) {
      if(test_range_quries && i>= DISTINCT_VALS_THRESH) {
        /* once we have created enough distinct values for f1 we will
         * simply pick a previous value */
        chosen_index = (rand_r(&thread_seed)) % DISTINCT_VALS_THRESH;
        len = 0;
        while(range_test_f1[chosen_index][len++]!=0) { continue; }
        memmove(f1, range_test_f1[chosen_index], len);
        len = 0;
        while(range_test_f2[chosen_index][len++]!=0) { continue; }
        memmove(f2, range_test_f2[chosen_index], len);
      } else {
        len = 7 + rand_r(&thread_seed)%10;
        /* utilize client_id, fiber_id to ensure different clients,threads generate
         * non-over lapping keys */
        f1[0] = (client_id & 0x0f) + 'a';
        f1[1] = (client_id>>4) + 'a';

        //turn f1[2] and f1[3] into characters 'a'-'z'
        f1[2] = ((fiber_id==-1)? thrd_id:fiber_id);
        f1[3] = (f1[2]>>4) + 'a';
        f1[2] = (f1[2] & 0x0f) + 'a';

        for(j=4; j<len; j++) {
          f1[j] = 'a' + rand_r(&thread_seed)%26;
        }
        f1[j] = '\0';

        if(test_range_quries) {
          /* IMPORTANT:: when utilizing a very large number of fibers, this
           * malloc() can give the appearence of a memory leak, in such cases
           * you can reduce the number of fibers that invoke this feature (by
           * adding code offcourse), or comment this feature out
           */
          range_test_f1[i] = (char*)malloc((len+1));
          memmove(range_test_f1[i], f1, len+1);
        }
        len = 5 + rand_r(&thread_seed)%10;
        for(j=0; j<len; j++) {
          f2[j] = 'a' + rand_r(&thread_seed)%26;
        }
        f2[j] = '\0';
        if(test_range_quries) {
          /* IMPORTANT:: when utilizing a very large number of fibers, this
           * malloc() can give the appearence of a memory leak, in such cases
           * you can reduce the number of fibers that invoke this feature (by
           * adding code offcourse), or comment this feature out
           */
          range_test_f2[i] = (char*)malloc((len+1));
          memmove(range_test_f2[i], f2, len+1);
        }
      }
      snprintf(f4, sizeof(f4), "10:01:34");
      snprintf(ip1, sizeof(ip1), "%d.%d.%d.%d",
               123, (((i>>16)+1)& 0xff), ((i>>8) & 0xff), (i & 0xff));
      snprintf(ip2, sizeof(ip2), "%d.%d.%d.%d",
               123, (((i>>16)+2)& 0xff), ((i>>8) & 0xff), (i & 0xff));

      std::string write_batchstr = "";
      write_batchstr = insert_qstr;

      int write_batchsz, tmp_deldrain, tmp_upddrain;
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
      //
      //BUG:: In some corner cases it is possible that preparing statements
      //outside the outermost for() loop below as well as within the loop can
      //lead to memory leak (ie preparing in both the places together, preparing
      //in only one spot is not an issue). If you observe this please report it.
      //
      cqldb::statement stat = cqldb_session << write_batchstr;

      len = 1;
      if(arg.larger_records) {
        len = (fast_rand32() % __MAX_DESC) - 1;
        if(len < 256) {
          len = 256; //since we are attempting larger records utilize atleast 128 bytes.
        }
      }
      for(j=0; j<len; j++) {
        //caution::synthetic field may be artificially more compressible, if
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
      stat << f1 << f2 << (i+100) << f4 << ip1 <<ip2;
      stat << descfield << listf << setf << mapf;

      if(delete_count==__NUM_DEL && update_count==__NUM_UPD) {
        //bind the parameters for the delete and update queries
        for(j=0; j< tmp_deldrain; j++) {
          stat << delete_f1[delete_drain_count+j] << delete_f2[delete_drain_count+j]
               << delete_f3[delete_drain_count+j] << delete_f4[delete_drain_count+j];
        }
        for(j=0; j< tmp_upddrain; j++) {
          stat << update_f1[update_drain_count+j] << update_f2[update_drain_count+j]
               << update_f3[update_drain_count+j] << update_f4[update_drain_count+j];
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
        /* notice that for stats queries within a read/write batch are counted individually
         * so a single batch of 4 selects is counted as 4 queries */
        cout << debug_tag << debug_id << " mixed RW/RD iter#:" << i
             << " select:" << sel_stats << " update:" << upd_stats
             << " delete:" << del_stats << " range-select:" << range_selects
             << " range-del:" << range_deletes 
             << " NUM-FIBERS:" << __NUM_FIBERS;
             //<< " num-flips:" << g_num_flips_performed;
#ifdef __PRINT_PACKET_STATS
        if(i%4096==0) {
          //print out packets stats even less often, as they can incurr system calls
          cqldb_session.packet_stats(&packets_sent, &packets_recvd, &packets_dropped);
          cout << " sent:" << packets_sent << " recvd:" << packets_recvd
               << " dropped:" << packets_dropped << endl;
        } else {
          cout << endl;
        }
#else
        cout << endl;
#endif
      }
      /* randomly decide if the value is going to be looked up */
      looked_up = 0;
      if(rand_r(&thread_seed)%2==0 && select_count < __NUM_SEL) {
        select_f1[select_count][0] = '\0';
        sprintf(&select_f1[select_count][0], "%s", f1);
        select_f2[select_count][0] = '\0';
        sprintf(&select_f2[select_count][0], "%s", f2);
        select_count++;
        looked_up = 1;
      }
      /* if the value is not going to be looked up then randomly decide if it
       * is going to be later updated/deleted */
      if(!looked_up) {
        switch(rand_r(&thread_seed)%8) {
         case 1:
           if(update_count < __NUM_UPD) {
             update_f1[update_count][0] = '\0';
             sprintf(&update_f1[update_count][0], "%s", f1);
             update_f2[update_count][0] = '\0';
             sprintf(&update_f2[update_count][0], "%s", f2);
             update_f3[update_count] = (i+100);
             update_f4[update_count][0] = '\0';
             sprintf(&update_f4[update_count][0], "%s", f4);
             update_count++;
           }
           break;
         case 2:
           if(delete_count < __NUM_DEL) {
             delete_f1[delete_count][0] = '\0';
             sprintf(&delete_f1[delete_count][0], "%s", f1);
             delete_f2[delete_count][0] = '\0';
             sprintf(&delete_f2[delete_count][0], "%s", f2);
             delete_f3[delete_count] = (i+100);
             delete_f4[delete_count][0] = '\0';
             sprintf(&delete_f4[delete_count][0], "%s", f4);
             delete_count++;
           }
           break;
         default:
           break;
        }
      }
      //NOTE:: This test is attempting to read data that has just been written
      //as a result these selects will not run into i/o and hence these select
      //tests do not stress random i/o at all
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
          //
          //BUG:: In some corner cases it is possible that preparing statements
          //outside the outermost for() loop below as well as within the loop can
          //lead to memory leak (ie preparing in both the places together, preparing
          //in only one spot is not an issue). If you observe this please report it.
          //
          cqldb::statement stat = cqldb_session << batchstr;

          //bind the select param using positional bind operator '<<'
          for(j=0; j<sel_batchsz; j++) {
            //NOTE::make certain that the parameters provided with query line up with
            //what is expected else you will get strange errors.
            stat << select_f1[select_drain_count+j];
            stat << select_f2[select_drain_count+j];
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
                cout << debug_tag << debug_id << " no results found for select" << endl;
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
          stat << select_f1[j];
          stat << select_f2[j];

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
              cout << debug_tag << debug_id << " no results found for select" << endl;
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

      if(test_range_quries && i>DISTINCT_VALS_THRESH) {
        //randomly decide if we are going to try a range-select or range-delete
        //we want to make these queries somewhat infrequent, so that the overall
        //test proceeds at a fair rate
        int num_range_sel_res=0;
        if(i%500==0) {

          //dynamically constructed queries have to be created within the loop because
          //we can not prepare them in advance outside the loop and hence we incurr the
          //cost of repeatedly preparing the queries.
          //
          //BUG:: In some corner cases it is possible that preparing statements
          //outside the outermost for() loop below as well as within the loop can
          //lead to memory leak (ie preparing in both the places together, preparing
          //in only one spot is not an issue). If you observe this please report it.
          //
          cqldb::statement stat = cqldb_session << select_range_qstr;

          chosen_index = (rand_r(&thread_seed)) % DISTINCT_VALS_THRESH;
          len = 0;
          while(range_test_f1[chosen_index][len++]!=0) { continue; }
          memmove(f1, range_test_f1[chosen_index], len);
          stat << f1;
          //optionally set a timeout of 10 seconds on the statement, this may be
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
        if(i%1000==0) {

          //dynamically constructed queries have to be created within the loop because
          //we can not prepare them in advance outside the loop and hence we incurr the
          //cost of repeatedly preparing the queries.
          //
          //BUG:: In some corner cases it is possible that preparing statements
          //outside the outermost for() loop below as well as within the loop can
          //lead to memory leak (ie preparing in both the places together, preparing
          //in only one spot is not an issue). If you observe this please report it.
          //
          cqldb::statement stat = cqldb_session << delete_range_qstr;

          chosen_index = (rand_r(&thread_seed)) % DISTINCT_VALS_THRESH;
          len = 0;
          while(range_test_f1[chosen_index][len++]!=0) { continue; }
          memmove(f1, range_test_f1[chosen_index], len);
          stat << f1;
          len = 0;
          while(range_test_f2[chosen_index][len++]!=0) { continue; }
          memmove(f2, range_test_f2[chosen_index], len);
          stat << f2;
          //optionally set a timeout of 10 seconds on the statement, this may be
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
    cout << debug_tag << debug_id << " SELECT #" << sel_stats << " UPDATE #" << upd_stats << " DELETE #" << del_stats;
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
    goto done;
  }
done:
  __LEAVE_FIBER;
  if(range_test_f1) {
    for(int k=0; k<DISTINCT_VALS_THRESH; k++) {
      free(range_test_f1[k]);
      range_test_f1[k] = NULL;
    }
    free(range_test_f1);
    range_test_f1 = NULL;
  }
  return;
}

/*
 * peachtest_driver_thread
 * a very basic test driver, it operates on very tiny data so it is not a real test
 * but it demonstrates the functionality. You could alter the test to generate a lot
 * of data as well and see if the functionality still works reasonably well.
 */
static void
peachtest_driver_thread(client_thrd_arg_t arg)
{
  char qbuff[8192];
  char* qstr = qbuff;
  int numres, numr;
  int readonly;
  int isread=0;
  int i=0, j, len;
  int client_id = arg.client_id;
  int fiber_id = arg.fiber_id;
  int thrd_id = arg.thrd_id;
  const char* debug_tag = (fiber_id==-1)? g_thread_tag: g_fiber_tag;
  int debug_id = (fiber_id==-1)? thrd_id: fiber_id;
  uint64_t packets_sent, packets_recvd, packets_dropped;
  unsigned int thread_seed;

  //remember the field1 value for the first few records
#define __FIRST_FEW 5
  char data_f1[__FIRST_FEW][__MAXLEN];
  char f1[__MAXLEN];
  char* tmp_f1;

  FILE* fp_dataq = fopen("./peachdb_testfiles/peach.data", "r");
  FILE* fp_selq = fopen("./peachdb_testfiles/peach.selq", "r");
  FILE* fp_updq = fopen("./peachdb_testfiles/peach.updq", "r");

  if(!fp_dataq || !fp_selq || !fp_updq) {
    printf("query files not found\n");
    exit(1);
  }

  //per thread initialization
  fast_randseed();
  //utilize thread_seed for rand_r() instead of rand() to avoid futex calls
  thread_seed = time(NULL) ^ (debug_id << 16);
  __START_FIBER;

  try {
    //session must be created before invoking the driver
    ostringstream session_params;
    session_params << "peachdb_cluster:"
                   << "cluster-configuration-path=" << cluster_configuration_path << ";"
                   << "self-configuration-path=" << self_configuration_path;

    //setup a clustered peachdb session, NOTE: session handles are not
    //thread safe, nor are any of the statement or any other handles.
    cqldb::session cqldb_session = cqldb::session(session_params.str());

    cout << debug_tag << debug_id << " loading initial data" << endl;
    i=0;
    while(test_get_next_query(fp_dataq, qbuff, sizeof(qbuff), &numres)==0) {
      if(i<__FIRST_FEW) {
        tmp_f1 = data_f1[i];
      } else {
        tmp_f1 = f1;
      }
      len = 7 + rand_r(&thread_seed)%10;
      /* utilize client_id, fiber_id to ensure different clients,threads generate
       * non-over lapping keys */
      tmp_f1[0] = (client_id & 0x0f) + 'a';
      tmp_f1[1] = (client_id>>4) + 'a';

      //turn tmp_f1[2] and tmp_f1[3] into characters 'a'-'z'
      tmp_f1[2] = ((fiber_id==-1)? thrd_id:fiber_id);
      tmp_f1[3] = (tmp_f1[2]>>4) + 'a';
      tmp_f1[2] = (tmp_f1[2] & 0x0f) + 'a';

      for(j=4; j<len; j++) {
        tmp_f1[j] = 'a' + rand_r(&thread_seed)%26;
      }
      tmp_f1[j] = '\0';
      cqldb::statement stat;
      stat = cqldb_session << qstr;
      stat.bind("field1_param", tmp_f1);
      //optionally set timeout between (120, 255) seconds, increase if client
      //runs into query timeout, to avoid resubmitting the query
      stat.set_timeout(120);
      do {
        //each iteration in this loop resubmits request to cluster
        stat.exec();
      } while(!stat.got_response() && __check_resubmit_write(stat));

      stat.reset(); //release statement resources, else memory leaks/corruption
      i++;
    }

    while(true) {
      if(!isread) {
        if(test_get_next_query(fp_updq, qbuff, sizeof(qbuff), &numres)!=0) {
          //if we are done with all the queries, we just start over
          rewind(fp_updq);
          isread = 1;
          continue;
        }
        readonly=0;
      } else {
        if(test_get_next_query(fp_selq, qbuff, sizeof(qbuff), &numres)!=0) {
          //if we are done with all the queries, we just start over
          rewind(fp_selq);
          isread = 0;
          continue;
        }
        readonly=1;
      }

      i++;
      if(i%1000==0) {
        cout << debug_tag << debug_id << " iter # for peachdb_testfile writes/selects: "
             << i << " NUM-FIBERS:" << __NUM_FIBERS;
#ifdef __PRINT_PACKET_STATS
        if(i%4096==0) {
          //print out packets stats even less often, as they can incurr system calls
          cqldb_session.packet_stats(&packets_sent, &packets_recvd, &packets_dropped);
          cout << " sent:" << packets_sent << " recvd:" << packets_recvd
               << " dropped:" << packets_dropped << endl;
        } else {
          cout << endl;
        }
#else
        cout << endl;
#endif
      }

      //statement is created in a try catch block so that when the block
      //ends, the resource held by the statement are automatically deallocated
      try {
        //cqldb_session << qstr << cqldb::exec; -- shorter syntax to execute query

        cqldb::statement stat;
        ::time_t now_time = std::time(0);
        std::tm now = *std::localtime(&now_time);

        stat = cqldb_session << qstr;

        //NOTE::make certain that the parameters provided with query line up with
        //what is expected else you will get strange errors.
        //bind some params just to check if we can do it
        stat.bind("p1", 20);
        stat.bind_null("p2");
        stat.bind("p3", now);
        stat.bind("p4", "Hello 'World'");
        cqldb::user_collection sub_coll = stat.create_user_collection();
        sub_coll.append(100);
        sub_coll.append("ilfjdh");
        sub_coll.append("ljl");
        cqldb::user_collection coll = stat.create_user_collection();
        coll.append(10);
        coll.append("abcd");
        coll.append("wikiki");
        coll.append(sub_coll);
        cqldb::user_collection rescoll = stat.create_user_collection();
        coll.fetch(3, rescoll);
        //int id;
        //string s1, s2;
        //rescoll >> id >> s1 >> s2;
        //cout << endl;
        //cout <<"id " << id << "s1 = " << s1.c_str() << "s2 = " << s2.c_str() << endl;
        //alternate syntax to append elements to a collection
        coll << 20 << "wahkarewarewa" << sub_coll << 23;
        //sub_coll append will take effect on all collections that
        //have appended it
        sub_coll.append("ljljkjk");
        stat.bind("field1_param", data_f1[0]);
        stat.bind("field1_param2", data_f1[1]);
        stat.bind("field1_param3", data_f1[2]);
        if(readonly) {
          //optionally set timeout between (120, 255) seconds, increase if client
          //runs into query timeout, to avoid resubmitting the query
          stat.set_timeout(120);
          do {
            //each iteration in this loop resubmits request to cluster
            stat.exec();
            //should we add a tiny usleep() before resubmitting request?
          } while(!stat.got_response());
        } else {
          //optionally set timeout between (120, 255) seconds, increase if client
          //runs into query timeout, to avoid resubmitting the query
          stat.set_timeout(120);
          do {
            //each iteration in this loop resubmits request to cluster
            stat.exec();
          } while(!stat.got_response() && __check_resubmit_write(stat));
        }
   
        if(stat.got_response()) {
          unsigned int flags;
          try {
            if(readonly) {
              cqldb::result res = stat.query();
              res.print_errors(flags);
              numr = 0;
              while(res.next()) {
                numr++;
              }
              //we can not validate results because multiple threads are involved
              //cout << "number of results =" << numr << endl;
              //cout << "expected number of results =" << numres << endl;
              //if(numres!=-1 && numres!=numr) {
                //exit(-1);
              //}
            } else {
              cqldb::result res = stat.query();
              res.print_errors(flags);
              if(flags) {
                cout << "some errors were found in query execution qstr:" << qstr << endl;
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
      } catch(exception const &e) {
        cerr << "ERROR: " << e.what() << endl;
        goto done;
      }
    }
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
    goto done;
  }
done:
  __LEAVE_FIBER;
  fclose(fp_dataq);
  fp_dataq = NULL;
  fclose(fp_selq);
  fp_selq = NULL;
  fclose(fp_updq);
  fp_updq = NULL;
  return;
}

/*
 * launch_client_thread_fibers
 * launch the specified number of fibers on the current thread
 * thrdid - client thread id
 * num_fibers - number of fibers to launch
 * first_fiberid - fiberid of the first fiber
 */
int
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
    /* choose only one of the below drivers for the client and all fibers of
     * all threads of all clients should utilize the same driver, thats how
     * the test is set up.
     */
    //all_fibers[i] = boost::fibers::fiber(custom_driver_thread, arg);
    //all_fibers[i] = boost::fibers::fiber(custom_update_driver_thread, arg);
    all_fibers[i] = boost::fibers::fiber(custom_mixload_driver_thread, arg);
    //all_fibers[i] = boost::fibers::fiber(custom_large_requests_driver_thread, arg);
    //all_fibers[i] = boost::fibers::fiber(peachtest_driver_thread, arg);
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
int
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
  //suggested_fibers = 8;

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
 
  //we will setup the minimal schema for all the tests. full_schema contains
  //a lot of secondary indexes and materialized views and is much slower and
  //is meant for the driver peachtest_driver_thread() to test functionality.
  //NOTE:: schema must be loaded only once before running any client threads
  peachtest_setup_schema(0);

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
int
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

  all_threads = new std::thread[max_threads];
 
  //we will setup the minimal schema for all the tests. full_schema contains
  //a lot of secondary indexes and materialized views and is much slower and
  //is meant for the driver peachtest_driver_thread() to test functionality.
  //NOTE:: schema must be loaded only once before running any client threads
  peachtest_setup_schema(0);

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
    /* choose only one of the below drivers for the client and all threads of
     * of all clients should utilize the same driver, thats how the test is
     * set up.
     */
    //all_threads[i] = std::thread(custom_driver_thread, arg);
    //all_threads[i] = std::thread(custom_update_driver_thread, arg);
    all_threads[i] = std::thread(custom_mixload_driver_thread, arg);
    //all_threads[i] = std::thread(custom_large_requests_driver_thread, arg);
    //all_threads[i] = std::thread(peachtest_driver_thread, arg);

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
   * be delayed till completion of the system call. If you do not want to deal
   * with co-operative scheduling then you can provide --no-fibers to this test
   * and then it will utilize only threads, in which you can make blocking calls.
   * Fibers are the preferable approach.
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
