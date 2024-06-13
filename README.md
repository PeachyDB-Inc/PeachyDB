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
