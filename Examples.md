# Examples

Below is the explanation of the examples that are available with the clients.

## Test 1
`peachdb_client_cqldb.cpp`

This is a stress test for the client and is meant to stress test various scenarios.

If you run the command `peachdb_client_cqldb` you get the following Usage:

“Usage: peachdb_client_cqldb <unique_client_id in range [0,255]> [–no-fibers] <br>
 unique_client_id is a client_id that has not been utilized so far on this or any other client <br>
 –no-fibers - (optional) if provided, utilize threads instead of fibers”


Each time the client `peachdb_client_cqldb` is executed a different `<unique_client_id>`
must be provided, this is because data stored in the database utilizes this id in
the primary key and if the same unique id is utilized repeatedly then all the data
will be duplicate of data written into the database in the previous invocation of
the client and all writes will fail.

Other parameters are self-explanatory.

This stress test contains a few different client drivers that are meant to stress
different use cases. Below is an explanation of these:

1. `custom_driver_thread` - primarily meant for insert performance testing
2. `custom_update_driver_thread` - stress tests update
3. `custom_large_requests_driver_thread` - stress test large client RPC requests (such
   requests which require more than 1300 bytes to encode). This is useful when submitting
   batch requests containing multiple queries to the servers.
4. `custom_mixload_driver_thread` - this is the stress test utilized most often to test
   a variety of different reads/writes and combinations thereof.

Most of the code has detailed comments to explain every aspect of the test.

The following files contain the schema for the client drivers:
- `./peachdb_testfiles/peach.schema.full`
- `./peachdb_testfiles/peach.schema`
- `./peachdb_testfiles/peach.security`

### Deployment Approach:

1. Start off with a run of `peachdb_client_cqldb` and to get a basic understanding
   of behavior and get a baseline.
2. Replace the schema files indicated above with your own schema.
3. Replace the client driver function with your own queries.
4. Possibly utilize separate threads/fibers for read queries and write queries.
5. Note that each invocation of `peachdb_client_cqldb` will reload the schema and
   return errors in reloading the schema since all the schema records are present.
   This should not hinder the test.
6. The numerous important aspects of running the test are described in the test,
   it would be better to carefully read through the comments to understand the issues.

## Test 2
`peachdb_client_udt.cpp`

This test is specifically provided to explain how to submit and extract UDTs
(user-defined types) when submitting queries and unpacking the responses for
the queries. The test is fairly detailed and self-explanatory. It is not meant
to be a stress test.
