/* peachydb_client_cqldb.cpp
 * Copyright(c) 2021 PeachyDB inc
 *
 * This file contains an example of client that executes UDT schema queries
 */

#include <stdio.h>
#include <signal.h>
#include <iostream>
#include <sstream>
#include <thread>

#include <boost/fiber/all.hpp>

#include "peachdb_test.hpp"
#include "cqldb/cqldb_api.h"

//some globals related to the client info
static const char* cluster_configuration_path = "/dbstore/mymisc/cluster_config.info";
static const char* self_configuration_path = "/dbstore/mymisc/my_config.info";

using namespace std;

/* here we have the schema specification */
const char* schema_queries [] = {
  "CREATE KEYSPACE school WITH REPLICATION = { 'class': 'SimpleStrategy', 'replication_factor': 3}",

  "CREATE TYPE school.Phone (country_code tinyint, number text)",

  "CREATE TYPE school.Address (street text, city  text, zip text, phones map<text, Phone>)",

  "CREATE TYPE school.Fullname (firstname text, lastname  text)",

  "CREATE Table school.Student (\
    person_id int,\
    dept_id int,\
    name Fullname,\
    title ascii,\
    enrollment DATE,\
    graduation DATE,\
    birth DATE,\
    currts timestamp,\
    currdate date,\
    currtime time,\
    fielduuid uuid,\
    fieldtimeuuid timeuuid,\
    addresses  map<text, frozen<Address>>,\
    emergency_contact  MAP<frozen<Fullname>, frozen<Address>>,\
    PRIMARY KEY (person_id, dept_id, birth)\
   )",

  (const char*)NULL
};

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
     //this can happen if there is a power failure and the cluster has lost
     //in memory information about whether the request was submitted or not
     //in this case the customer must provide code (database read query to execute)
     //to determine whether or not the transaction was applied to the cluster.
     //
     //Hopefully the answer will indicate what happened. The reason for 'hopefully'
     //is that due to snapshot isolation it is possible that reads will return
     //data that is not most recently committed. Maybe non-snapshot isolation
     //request will work better in this case. For the moment we will do the
     //safer thing and not resubmit the request
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
 * udt_setup_schema
 * upload the schema before performing any queries.
 */
static int
udt_setup_schema(void)
{
  int i=0;

  cout << "Setting up initial schema" << endl;

  try {
   //session must be created before invoking the driver
   ostringstream session_params;
   session_params << "peachdb_cluster:"
                  << "cluster-configuration-path=" << cluster_configuration_path << ";"
                  << "self-configuration-path=" << self_configuration_path;

   //setup a clustered peachdb session, NOTE: session handles are not
   //thread safe, nor are any of the statement or any other handles.
   cqldb::session cqldb_session = cqldb::session(session_params.str());

   while(schema_queries[i]) {
     try {
       cqldb::statement stat;
       stat = cqldb_session << schema_queries[i];
       do {
         //each iteration in this loop resubmits request to cluster
         stat.exec();
       } while(!stat.got_response() && __check_resubmit_write(stat));
       stat.reset(); //release statement resources, else memory leaks/corruption
     } catch(exception const &e) {
       cerr << "ERROR: " << e.what() << endl;
     }
     i++;
   }

   /* we have to update the client info with schema changes before we
    * can perform any queries that involve the schema, this is an
    * expensive operation and should not be executed frequently */
   if(cqldb_session.refresh_metadata()!=0) {
     cerr << "unable to obtain refreshed meta info" << endl;
     exit(-1);
   }
  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
  }
done:
  return 0;
}

/* BEGIN:: API to set collection fields for a UDT */
static inline void
set_phone(cqldb::user_collection& coll, const char* country_code, const char* number)
{
  //UDT is specified in the form of name value pairs
  coll << "country_code" << country_code << "number" << number;
}

static inline void
set_fullname(cqldb::user_collection& coll, const char* first_name, const char* last_name)
{
  //UDT is specified in the form of name value pairs
  coll << "firstname" << first_name << "lastname" << last_name;
}

static inline void
set_address(cqldb::user_collection& coll, const char* street, const char* city,
            const char* zip, cqldb::user_collection& phones)
{
  //UDT is specified in the form of name value pairs
  coll << "street" << street << "city" << city << "zip" << zip << "phones" << phones;
}
/* END:: API to set collection fields for a UDT */


/*
 * udt_execute_queries
 * execute UDT queries, insert,select
 */
static void
udt_execute_queries(void)
{
  int len,i,j;
  try {
    //session must be created before invoking the driver
    ostringstream session_params;
    session_params << "peachdb_cluster:"
                   << "cluster-configuration-path=" << cluster_configuration_path << ";"
                   << "self-configuration-path=" << self_configuration_path;

    //setup a clustered peachdb session, NOTE: session handles are not
    //thread safe, nor are any of the statement or any other handles.
    cqldb::session cqldb_session = cqldb::session(session_params.str());

    static const char* insert_qstr= "INSERT INTO school.Student (person_id, dept_id, name, title, enrollment, graduation, birth, currts, currdate, currtime, fielduuid, fieldtimeuuid, addresses, emergency_contact) VALUES ('1', '2', ?, 'senior',  '2014-07-28', '2018-07-20', '1997-03-04', currentTimestamp(), currentDate(), currentTime(), fnuuid(), currentTimeUUID(), ?, ?)";

    cqldb::statement stat = cqldb_session << insert_qstr;

    cqldb::user_collection home_phone = stat.create_user_collection();
    set_phone(home_phone, "1", "408-225-1350");

    cqldb::user_collection home_phone_mapf = stat.create_user_collection();
    home_phone_mapf << "home" << home_phone; 

    cqldb::user_collection name_coll = stat.create_user_collection();
    set_fullname(name_coll, "James", "Smith");

    cqldb::user_collection home_address_coll = stat.create_user_collection();
    set_address(home_address_coll, "4052 patrick henry dr", "santa clara", "95044", home_phone_mapf);

    cqldb::user_collection work_phone = stat.create_user_collection();
    set_phone(work_phone, "1", "408-865-4232");

    cqldb::user_collection work_phone_mapf = stat.create_user_collection();
    work_phone_mapf << "work" << work_phone;

    cqldb::user_collection work_address_coll = stat.create_user_collection();
    set_address(work_address_coll, "6852 worthington dr", "santa clara", "95080", work_phone_mapf);

    cqldb::user_collection address_collf = stat.create_user_collection();
    address_collf << "home" << home_address_coll << "work" << work_address_coll;

    cqldb::user_collection emergency_name_coll = stat.create_user_collection();
    set_fullname(emergency_name_coll, "Carol", "McCall");

    cqldb::user_collection emergency_phone = stat.create_user_collection();
    set_phone(emergency_phone, "1", "408-875-4825");

    cqldb::user_collection emergency_phone_mapf = stat.create_user_collection();
    emergency_phone_mapf << "Carol" << emergency_phone;

    cqldb::user_collection emergency_address_coll = stat.create_user_collection();
    set_address(emergency_address_coll, "2012 warburton dr", "santa clara", "95010", emergency_phone_mapf);

    cqldb::user_collection emergency_contact_collf = stat.create_user_collection();
    emergency_contact_collf << emergency_name_coll << emergency_address_coll;

    stat << name_coll << address_collf << emergency_contact_collf;

    /******
     alternate syntax to achieve the same
     stat.bind(name_coll);
     stat.bind(address_collf);
     stat.bind(emergency_contact_collf);
    ******/

    cout << endl;
    cout << " about to execute query:" << endl << insert_qstr << endl;

    do {
      //each iteration in this loop resubmits request to cluster
      stat.exec();
    } while(!stat.got_response() && __check_resubmit_write(stat));

    try {
      cqldb::result res = stat.query();
      unsigned int flags;
      do {
        res.print_errors(flags);
        if(flags==CQLDB_HAS_LIMIT) {
          //if an update/delete modifies multiple objects there is a chance
          //that there are more objects that match the where clause than a
          //statement can update in a single transaction. In this case the
          //user can reissue the write query untill it no longer runs into
          //the limit (note, such updates/deletes can not be part of a batch)
          cout << " write statement applied to limited(1000) objects" << endl;
        } else if(flags==CQLDB_WR_SKIPPED) {
          //it is possible that within a batch of writes an update/delete/insert
          //may not get applied due to key conflicts etc. In such cases that
          //statement in the batch will be skipped but the rest of the batch
          //will still be applied.
          cout << " write statement skipped" << endl;
        }
      } while(res.next_stmt());
    } catch(exception const &e) {
      //we will not terminate, the client we will keep moving on
      cerr << "ERROR: " << e.what() << endl;
    }

    //always reset current statment before moving onto the next, this will 
    //release all resources including all collections.
    stat.reset(); //release statement resources, else memory leaks/corruption

    sleep(1); //just for testing

    //This query is formulated to demonstrate how to extract values from UDT
    //specifying select without where clause constraints is not expected to perform
    static const char* select_qstr= "SELECT name, enrollment, graduation, birth, currts, currdate, currtime, fielduuid, fieldtimeuuid, addresses, emergency_contact FROM school.Student";

    stat = cqldb_session << select_qstr;
    cout << "about to execute:" << endl << select_qstr << endl;

    do {
      //each iteration in this loop resubmits request to cluster
      stat.exec();
      //should we add a tiny usleep() before resubmitting request?
    } while(!stat.got_response());

    try {
      unsigned int flags;
      cqldb::result select_res = stat.query();

      //print any reported errors
      select_res.print_errors(flags);

      if(!select_res.next()) {
        cout << " no response found" << endl;
        goto done;
      }
      cqldb::user_collection rescoll_name = stat.create_user_collection();
      cqldb::user_collection rescoll_addresses = stat.create_user_collection();
      cqldb::user_collection rescoll_emergency = stat.create_user_collection();
      std::string enrollment, graduation, birth;
      std::string currts, currdate, currtime;
      std::string fielduuid, fieldtimeuuid;
      std::string tag, address_tag, first_name, last_name;
      std::string street, city, zip;
      int country_code; //note that we have to utilize numeric according to schema definition
      std::string number;

      select_res >> rescoll_name >> enrollment >> graduation
                 >> birth >> currts >> currdate >> currtime
                 >> fielduuid >> fieldtimeuuid
                 >> rescoll_addresses >> rescoll_emergency;

      //UDT fields extracted in order of schema definition
      rescoll_name >> first_name >> last_name;

      cout << "Fullname: " << first_name << " " << last_name << endl;
      cout << "enrollment: " << enrollment << endl;
      cout << "graduation: " << graduation << endl;
      cout << "DOB: " << birth << endl;

      cout << "currts: " << currts << endl;
      cout << "currdate: " << currdate << endl;
      cout << "currtime: " << currtime << endl;
      cout << "UUID: " << fielduuid << endl;
      cout << "TMUUID: " << fieldtimeuuid << endl;

      len = rescoll_addresses.length();
      for(i=0; i<len; i+=2 /* we are hopping key,val of a map */) {
        cqldb::user_collection address = stat.create_user_collection();
        cqldb::user_collection phones = stat.create_user_collection();
        rescoll_addresses >> address_tag >> address;

        //UDT fields extracted in order of schema definition
        address >> street >> city >> zip >> phones;
        cout << "Address: " << address_tag << ": " << street << ", " << city << ", " << zip << endl;
        cout << "Phones:" << endl;

        for(j=0; j<phones.length(); j+=2) {
          cqldb::user_collection phone = stat.create_user_collection();
          phones >> tag >> phone;
          phone >> country_code >> number;
          cout << tag << ": " << country_code << "-" << number << endl;
        }
      }

      len = rescoll_emergency.length();
      for(i=0; i<len; i+=2 /* we are hopping key,val of a map */) {
        cqldb::user_collection emergency_name = stat.create_user_collection();
        cqldb::user_collection emergency_address = stat.create_user_collection();
        cqldb::user_collection emergency_phones = stat.create_user_collection();
        rescoll_emergency >> emergency_name >> emergency_address;

        //UDT fields extracted in order of schema definition
        emergency_name >> first_name >> last_name;
        cout << "Emergency Contact" << endl;
        cout << "Fullname: " << first_name << " " << last_name << endl;

        //UDT fields extracted in order of schema definition
        emergency_address >> street >> city >> zip >> emergency_phones;
        cout << "Address: " << street << ", " << city << ", " << zip <<endl;
        cout << "Phones: ";
        for(j=0; j<emergency_phones.length(); j+=2 /* we are hopping key,val of a map*/) {
          cqldb::user_collection emergency_phone = stat.create_user_collection();
          emergency_phones >> tag >> emergency_phone;
          emergency_phone >> country_code >> number;
          cout << tag << ": " << country_code << "-" << number << endl;
        }
        if(i>0) {
          cout << ", ";
        }
      }
    } catch(exception const &e) {
      //we will not terminate, the client we will keep moving on
      cerr << "ERROR: " << e.what() << endl;
    }
    //always reset current statment before moving onto the next, this will 
    //release all resources including all collections.
    stat.reset(); //release statement resources, else memory leaks/corruption

  } catch(exception const &e) {
    cerr << "ERROR: " << e.what() << endl;
    goto done;
  }
done:
  return;
}

int
main(int argc, const char *argv[])
{
  /* Block SIGPIPE, so that any reads/writes don't terminate the client
   * but rather return EPIPE error, alternately the client can choose
   * to handle the signal with a signal handler
   */
  if(signal(SIGPIPE, SIG_IGN)==SIG_ERR) {
    cerr << "Unable to set ignore SIGPIPE" << endl;
    exit(-1);
  }
  udt_setup_schema();
  udt_execute_queries();
  return 0; 
}
