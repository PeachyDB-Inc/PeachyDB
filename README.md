# PeachyDB
Databases and things
# Customer Advice

PeachyDB is a horizontally scalable database intended to overcome the shortcomings of Apache Cassandra. We are mostly compatible with CQL, differences are noted below. This note contains various issues that a customer should be aware of when utilizing this product. All the commands and examples to run a cluster and drive it with a client are provided in a separate document. Reading this document is a pre-requisite to understanding this product.

**Prerequisite**: Please read Apache Cassandra CQL documentation to understand the below.

**Product Status**: Beta release.

## 1. Apache Cassandra features currently not implemented:
- TTL
- Write-timestamp
- Statics
- Counters
- Json input/output
- Any java related things: user-defined-functions, user-defined-aggregates.
- Backup/Restore is not yet implemented.
- Any data encryption features are not available.

## 2. Currently only a C++ client is available.
Examples provide a clear explanation of how to utilize a client to drive load to servers.
- **2.a)** User-defined-type, collection-types are supported.
- **2.b)** A primary key column of a Base Table or a Materialized View cannot be of type user-defined-type, or a collection-type. This is the same as Apache Cassandra (up to the most recent versions of CQL, if this has changed since please send me a note).
- **2.c)** Partition key, primary key, clustering key have the same meaning as in Apache Cassandra.
- **2.d)** 'table_options' of CQL for creating Tables, Materialized views are ignored.

## 3. Materialized Views (MV) and Secondary Indexes are supported and behave similarly to Apache Cassandra.
- **3.a)** MV is the same as the base table, with possibly a different partition key and clustering key than the base table (the clustering key is likely different from the base table, else there are very few reasons to create the MV).
- **3.b)** MV partition key, primary key are subject to the same requirements as Cassandra.
- **3.c)** UPDATE/DELETE/INSERT cannot be directly invoked on Materialized View, MVs are automatically maintained based upon write queries on the base table.
- **3.d)** SELECT queries can be directly invoked on MV (ONLY THEN will MV be utilized for a SELECT, otherwise not).
- **3.e)** When creating MV, table-options (Compact/Cluster/...) are ignored. Once an MV schema record has been created it can be deleted but it cannot be modified.
- **3.f)** IMPORTANT: At the moment, in order to avoid significant performance penalties we will include all columns of the base table in the MV. For the moment this will cause extra storage to be taken up by the MV. We know this limitation. In the subsequent releases, we plan to address issues surrounding the MV concept.
  - **3.f.1)** There is a known bug that after MV has been created if more fields are added/removed from the base table schema, they do not automatically get added/removed from MV schema, as a result, any subsequent DELETE/UPDATE should not have a WHERE clause or IF clause that refers to the newly added fields, else the MV records may not get deleted/updated.
  - **3.f.2)** IMPORTANT: At the moment any Materialized Views needed must be provided with the initial schema utilizing the CREATE MATERIALIZED VIEW statement. You cannot ADD a materialized view later on in a running product. You can DROP an MV in a running database but you cannot ADD an MV at the moment. We have to work on adding this functionality soon. Also, the product currently does NOT validate that you are not allowed to add MV later on, so you will be able to add it but it will report incorrect results. This is a known issue that we have to resolve.
- **3.g)** For Secondary Index clustering-order specification is ignored (shouldn't matter).
- **3.h)** Secondary Index can only be created on a base table not an MV.
- **3.i)** Secondary Index on each node is created for records that are local to that node.
- **3.j)** Basically 3.d) means that Secondary Index is sharded on the same key as the base table.
- **3.k)** Secondary indexes of numerous kinds on collections are supported.
- **3.l)** Secondary index on user-defined-type is not supported because there is no natural sort order of user-defined-type. This is the same as Apache Cassandra.

## 4. INSERT, DELETE, UPDATE behave differently from Apache Cassandra as explained:
- **4.a)** Unlike Apache Cassandra INSERT is an INSERT not an UPSERT, if the primary key conflicts with an existing object INSERT will fail.
- **4.b)** Unlike Apache Cassandra UPDATE is an UPDATE not an UPSERT. So if the underlying item doesn't exist UPDATE will fail.
- **4.c)** UPDATE query can modify any columns other than the columns that form the partition key of the base table or columns that form the partition key of any MVs on the base table (this is less restrictive than Apache Cassandra which does not allow modifying any primary key column of the base table).
- **4.d)** If DELETE/UPDATE is part of a write batch then DELETE/UPDATE must also specify all the primary key columns in the WHERE clause exactly. As a part of the write batch, DELETE can delete only one object at a time. UPDATE can update only one object at a time.
- **4.e)** If DELETE/UPDATE is not part of a batch then as explained in 4.f) it can modify multiple objects.

- **4.f)** On user request, we have allowed the possibility of range DELETE (delete which allows deleting a range of objects (or multiple objects) with a single DELETE query. We have also made a similar extension for UPDATE query). However, currently, it has the following limitations:
  - **4.f.1)** Range delete cannot be part of a write batch. It has to be its own write transaction, this is because in the unlikely event of range delete failure, we do not want to confuse the rest of the writes in the batch.
  - **4.f.2)** Range delete will only delete 1K records at a time on any given server and commit the txn and return if it ran into the 1K limit on that server.
  - **4.f.3)** UPDATE query that modifies multiple objects is also subject to 4.f.1 and 4.f.2. Additionally, such an UPDATE query cannot modify any of the primary key columns of the base table or that of any of the MVs on the base table.
  - **4.f.4)** If deleting/updating a range of objects or multiple objects runs into the 1k limit, you can reissue the delete/update till all objects of interest have been deleted.
  - **4.f.5)** When deleting/updating a range of objects, the WHERE clause must also:
    - **4.f.5.1)** Restrict all partition key columns of the base table as well as MVs on the base table with equality constraints.
    - **4.f.5.2)** Restricting any secondary index key in addition to 4.f.5.1) can also help with the performance of base table record changes.
    - **4.f.5.3)** WHERE clause should restrict as many columns of the primary key as possible in order to improve its efficiency. Primary key columns must be restricted in order of the primary key. If one of the primary key columns is not restricted then any of the subsequent primary key columns restrictions (in order of the primary key) will not be utilized for scanning the database.
    - **4.f.5.4)** A WHERE clause which doesn't have enough restrictions in accordance with 4.f.5.1) will be rejected.
    - **4.f.5.5)** If enough primary key columns are not restricted the query could become quite slow because it might end up scanning large swaths of the database to find objects to modify.
    - **4.f.5.6)** Example to clarify primary key restrictions. If a table has primary key columns A, B, C, D, E (in order). If a WHERE clause restricts A, B, D, E then only restrictions on A, B can be utilized effectively for query performance, however, if your usage requires D, E restrictions for correctness please include them as well. Equality restrictions perform the best and are required on partition key.
- **4.f.6)** In the unlikely event of failure of deletion/update of any one record, the operation will be aborted only on those database servers where it encountered a failure. It will still be applied on all the other servers that succeeded in applying it. Failure of DELETE/UPDATE should not happen. However, bugs in software could possibly cause this situation. If the user does notice this, they should find out why the delete failed on any server, if they can, and resubmit the DELETE/UPDATE with the issue addressed. If they see no reason for failure then file a bug for us to fix.
- **4.f.7)** NOTE: The above functionality to delete multiple objects is provided only when deleting the object. If you are deleting only some fields of an object and not the whole object, that is actually treated as an UPDATE and it is still subject to all requirements of an UPDATE statement.
- **4.f.8)** We understand these limitations and will improve upon these later on. For the moment the user has to work with these limitations. It helps with a lot of use cases as a result it is better to provide DELETE/UPDATE on multiple objects functionality with the above limitations rather than not providing it at all. But the limitations are real and have to be alleviated. We will do that as soon as we can.

- **4.g)** The "IF syntax" of DELETE, UPDATE statements has no meaning, simply add all conditions to the WHERE clause of DELETE/UPDATE, it will not incur any additional overhead. There is only one use case for IF syntax. The IF syntax allows expressions involving user-defined-type, whereas WHERE clause grammar in CQL does not allow it. If you have such use case then utilize IF syntax, else place conditions in WHERE clause. It has no implications related to Paxos. IMPORTANT: DO NOT include any primary key constraints in the IF clause, you WILL get errors, place all those constraints in WHERE clause.

- **4.h)** Unlike Apache Cassandra, data modification should not generate severe performance problems, unless the WHERE clause for DELETE/UPDATE does not have enough restrictions on primary key columns and the query ends up scanning large swaths of the database.
- **4.i)** Apache Cassandra will often times truncate timestamp value to milliseconds, we do the same.
- **4.j)** A DATEMSK environment variable points to the DATEMSK file, which allows the following formats (%A, %T,%F, %FT%T). We will allow replacing this file later on.

## 5. Select queries work under similar constraints as Apache Cassandra.
- **5.a)** SELECT query that has WHERE clause conditions restricting all the partition key constraints can perform better than others because they can be sent to specific nodes.
- **5.b)** SELECT query that has WHERE clause conditions which also restrict some of the primary key columns (in addition to the partition key columns) can perform even better.
- **5.c)** SELECT query can be invoked on Materialized View instead of base table. Such queries can also benefit if partition key of MV is restricted along with any of the other primary key columns.
- **5.d)** SELECT query which does not restrict all of the partition key columns will perform the worst, as the query has to be executed on all nodes that could potentially contain the answer.
- **5.e)** SELECT query that could potentially involve scanning a lot of data has to be marked ALLOW FILTER (just as in Apache Cassandra)
- **5.f)** Occasionally due to networking issues or other causes the SELECT query may timeout or have partial results. In such cases, a flag is set on the query results which can determine if the results were partial or had timed out.

NOTE: Depending upon the use case MV can perform better or Secondary index can perform better. Examine the where clause of your queries to determine which is better.
  - **5.f.1)** If your query restricts all columns of partition key and secondary index column then the query will be sent to a single node and will be fast enough.
  - **5.f.2)** If the query does not restrict all columns of partition key but restricts secondary index column then it will have to be sent to all nodes that could possibly contain the result, this will make it slower (but still likely much better than Apache Cassandra).
  - **5.f.3)** If the query restricts columns in a different order than the clustering order of columns in the base table, then an MV could perform better.
  - **5.f.4)** SELECT on MV has to be explicitly invoked on the MV (not on the base table).
  - **5.f.5)** IMPORTANT: MV as well as Secondary indexes take up a lot of space, utilize them after carefully examining your queries. The more secondary indexes and MVs you create on a single table the more it will degrade the write performance. (This is true of all other databases as well).

## 6. Transactions are of two kinds (read-only and read-write):
- **6.a)** Batch of writes
  - **6.a.1)** Can contain a sequence of insert, delete, update queries (no selects).
  - **6.a.2)** IMPORTANT: If one of the writes in the batch fails to apply, it will be skipped and the user will be informed that it was skipped, however, the rest of the batch will be applied. This is not ideal. However, this is a first step that is likely to meet several use cases.
  - **6.a.3)** At the moment, the write batch query string, including space for query parameters and all other encodings should not be more than 320KB in size.
  - **6.a.4)** At the moment, range deletes or range updates are not allowed in a batch of writes, in other words, a delete/update query must specify all the primary key members in the Where clause in order to alter only a single object that matches the primary key.
  - **6.a.5)** The user can specify range DELETE/UPDATE as a separate write txn (NOT as part of a write Batch, where it will be rejected). The functionality is available in a limited form as described above.
  - **6.a.6)** With the exception of unappliable writes being skipped, the batch will be applied as a whole (in other words readers will not see partial results of a batch).
- **6.b)** Batch of read-only transactions - transactions that perform only reads. Currently, such batches which have even one query that has ALLOW-FILTER will not return results that are transactional (We will improve this later). The rest of the transactions will return MVCC (multi-version concurrency control) consistent results. (MVCC is the same as snapshot isolation). MVCC read-only transactions return snapshot isolation consistent results, which can return data which is older than the most recently committed.

- **6.c)** The user can check on the SELECT query results if it was transactional or not.
- **6.d)** Write batch can contain only UPDATE/DELETE/INSERT queries, read batch can contain only SELECTs, also, schema/security-data queries cannot be part of a batch.
- **6.e)** Transactions utilize distributed consensus, as a result, the database will become unavailable for writes if the majority of the coordinator nodes are down. This is the price to pay for consistency. Some of this is mitigated with a larger number of coordinator nodes.
- **6.f)** Write transactions will respond to the client as soon as each statement in the batch has been applied on some node, however, it will not wait for it to be applied on all applicable nodes before responding to the client. Writes are applied only if a quorum of coordinators have received the write txn and marked it as to-be-applied. Thus as soon as the write returns if you submit an MVCC read-only query to obtain the modified data right away you might get older data. That is just the nature of MVCC, it is not guaranteed to return the most recently committed data. This is true of all MVCC implementations.
- **6.g)** Transaction functionality is rather limited, this is because this is a first implementation. We will make it better later.

