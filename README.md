# PeachyDB

![Logo](/LogoPeachyDB.png)
<p align="center">
  <img src="https://github.com/akseg73/PeachyDB/blob/main/LogoPeachyDB.png" width="150" alt="Centered Logo">
</p>

PeachyDB is a horizontally scalable database intended to overcome the shortcomings of Apache Cassandra. We are mostly compatible with CQL, differences are noted below. This note contains various issues that a customer should be aware of when utilizing this product. All the commands and examples to run a cluster and drive it with a client are provided here. Reading this document is a pre-requisite to understanding the product.

**Prerequisite**: Please read <a href="https://cassandra.apache.org/doc/latest/cassandra/developing/cql/index.html" title="Documentation"> Apache Cassandra CQL documentation </a> to understand the below.

**Product Status**: Beta release.

<h2>Table of Contents</h2>
<ul>
  <li><a href="#1-apache-cassandra-features-not-implemented">1. Apache Cassandra features not implemented</a></li>
  <li><a href="#2-only-c-client-available">2. Clients Available</a></li>
  <li><a href="#3-materialized-views-mv-and-secondary-indexes-si">3. Materialized Views (MV) and Secondary Indexes (SI)</a></li>
  <li><a href="#4-insert-delete-update">4. INSERT, DELETE, UPDATE</a></li>
  <li><a href="#5-select">5. SELECT</a></li>
  <li><a href="#6-database-transactions-read-only-and-read-write">6. Database Transactions </a></li>
  <li><a href="#7-partition-key">7. Partition Key</a></li>
  <li><a href="#8-schema-changes">8. Schema Changes</a></li>
  <li><a href="#9-client-refresh">9. Client Refresh</a></li>
  <li><a href="#10-replication-factor">10. Replication Factor</a></li>
  <li><a href="#11-co-ordinator-nodes">11. Coordinator Nodes</a></li>
  <li><a href="#12-durability">12. Durability</a></li>
  <li><a href="#13-cluster-size-limit">13. Cluster Size Limit</a></li>
  <li><a href="#14-adding-nodes-to-the-cluster">14. Adding Nodes to Cluster</a></li>
  <li><a href="#15-replacing-a-failed-node">15. Replacing Failed Node</a></li>
  <li><a href="#16-removing-nodes-from-the-cluster">16. Removing Nodes from Cluster</a></li>
  <li><a href="#17-datacenter-clusters">17. Datacenter Clusters</a></li>
  <li><a href="#18-monitoring-nodes">18. Monitoring Nodes</a></li>
  <li><a href="#19-interference-with-database-servers">19. Interference with Database Servers</a></li>
  <li><a href="#20-aws-instance-types-for-database-servers-clients">20. AWS Instance Types allowed </a></li>
  <li><a href="#21-cloudformation-effectiveness">21. CloudFormation Effectiveness</a></li>
  <li><a href="#22-backup-restore">22. Backup, Restore</a></li>
  <li><a href="#23-deployment-strategy">23. Deployment Strategy</a></li>
  <li><a href="#24-known-bugs-issues">24. Known Bugs, Issues</a></li>
  <li><a href="#25-limits">25. Limits</a></li>
  <li><a href="#26-security-issues">26. Security Issues</a></li>
  <li><a href="#27-costs">27. Costs</a></li>
  <li><a href="#28-support-and-license">28. Support and License</a></li>
  <li><a href="#29-benchmarks">29. Benchmarks</a></li>
  <li><a href="#30-why-are-we-closed-source">30. Open source?</a></li>
</ul>


## 1. Apache Cassandra features not implemented
- TTL
- Write-timestamp
- Statics
- Counters
- Json input/output
- Any java related features: user-defined-functions, user-defined-aggregates, triggers.
- Backup/Restore is not yet implemented.
- Any data encryption features are not available.

## 2. Only C++ client available 
Client Driver <a href="https://github.com/akseg73/PeachyDB/tree/main/examples" title="Documentation"> Examples</a> provide a detailed explanation of how to drive load to servers. Some noteworthy points are as below:
- **2.a)** User-defined-type, collection-types are supported.

- **2.b)** A primary key column of a Base Table or a Materialized View cannot be of type user-defined-type, or a collection-type. This is the same as Apache Cassandra (up to the most recent versions of CQL, if this has changed since please open a case).

- **2.c)** Partition key, primary key, clustering key have the same meaning as in Apache Cassandra.

- **2.d)** 'table_options' of CQL for creating Tables, Materialized views are ignored as they have no meaning in our case.

## 3. Materialized Views (MV) and Secondary Indexes (SI)
MV and SI behave similarly to Apache Cassandra with some differences noted below.
- **3.a)** MV is the same as the base table, with possibly a different partition key and clustering key than the base table (the clustering key is likely different from the base table, else there are very few reasons to create the MV).
  
- **3.b)** MV partition key, primary key are subject to the same requirements as Cassandra.

- **3.c)** UPDATE/DELETE/INSERT cannot be directly invoked on Materialized View, MVs are automatically maintained based upon write queries on the base table.

- **3.d)** SELECT queries can be directly invoked on MV (ONLY THEN will MV be utilized for a SELECT, otherwise not).

- **3.e)** When creating MV, table-options (Compact/Cluster/...) are ignored. Once an MV schema record has been created it can be deleted but it cannot be modified.

- **3.f)** **Important**: At the moment, in order to avoid significant performance penalties we will include all columns of the base table in the MV. For the moment this will cause extra storage to be taken up by the MV. We know this limitation. In the subsequent releases, we plan to address issues surrounding the MV concept.
  - **3.f.1)** There is a known bug that after MV has been created if more fields are added/removed from the base table schema, they do not automatically get added/removed from MV schema, as a result, any subsequent DELETE/UPDATE should not have a WHERE clause or IF clause that refers to the newly added fields, else the MV records may not get deleted/updated.

  - **3.f.2)** **Important**: At the moment any Materialized Views needed must be provided with the initial schema utilizing the CREATE MATERIALIZED VIEW statement. You cannot ADD a materialized view later on in a running product. You can DROP an MV in a running database but you cannot ADD an MV at the moment. We have to work on adding this functionality soon. Also, the product currently does NOT validate that you are not allowed to add MV later on, so you will be able to add it but it will report incorrect results. This is a known issue that we have to resolve.

- **3.g)** For Secondary Index clustering-order specification is ignored (shouldn't matter).

- **3.h)** Secondary Index can only be created on a base table not an MV.

- **3.i)** Secondary Index on each node is created for records that are local to that node.

- **3.j)** Basically 3.i) means that Secondary Index is sharded on the same key as the base table.

- **3.k)** Secondary indexes of numerous kinds on collections are supported.

- **3.l)** Secondary index on user-defined-type is not supported because there is no natural sort order of user-defined-type. This is the same as Apache Cassandra.
- **3.m)** If MV has the same partition key as the base table, is there any need for MV at all, would SI suffice?
- **3.n)** As with all databases utilizing SI, MV has a performance penalty on writes in exchange for improvements in read performance. The more SI,MV on a single table the more the performance loss on writes.

## 4. INSERT, DELETE, UPDATE
Insert, Delete, Update work differently from Apache Cassandra.
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
- **4.f.6)** In the likely uncommon event of failure of deletion/update of any one record, the operation will be aborted only on those database servers where it encountered a failure. It will still be applied on all the other servers that succeeded in applying it. Failure of DELETE/UPDATE should not happen. However, bugs in software could possibly cause this situation. If the user does notice this, they should find out why the delete failed on any server, if they can, and resubmit the DELETE/UPDATE with the issue addressed. If they see no reason for failure then file a bug for us to fix.
- **4.f.7)** NOTE: The above functionality to delete multiple objects is provided only when deleting the object. If you are deleting only some fields of an object and not the whole object, that is actually treated as an UPDATE and it is still subject to all requirements of an UPDATE statement.
- **4.f.8)** We understand these limitations and will improve upon these later on. For the moment the user has to work with these limitations. It helps with a lot of use cases as a result it is better to provide DELETE/UPDATE on multiple objects functionality with the above limitations rather than not providing it at all. But the limitations are real and have to be alleviated. We will do that as soon as we can.

- **4.g)** The "IF syntax" of DELETE, UPDATE statements has no meaning, simply add all conditions to the WHERE clause of DELETE/UPDATE, it will not incur any additional overhead. There is only one use case for IF syntax. The IF syntax allows expressions involving user-defined-type, whereas WHERE clause grammar in CQL does not allow it. If you have such use case then utilize IF syntax, else place conditions in WHERE clause. It has no implications related to Paxos. **Important**: DO NOT include any primary key constraints in the IF clause, you WILL get errors, place all those constraints in WHERE clause.

- **4.h)** Data modification should not generate severe performance problems, unless the WHERE clause for DELETE/UPDATE does not have enough restrictions on primary key columns and the query ends up scanning large swaths of the database.

- **4.i)** Apache Cassandra will often times truncate timestamp value to milliseconds, we do the same.

- **4.j)** A DATEMSK environment variable points to the DATEMSK file, which allows the following formats (%A, %T,%F, %FT%T). We will allow replacing this file later on.

## 5. SELECT
Select works under similar constraints as Apache Cassandra
- **5.a)** SELECT query that has WHERE clause conditions restricting all the partition key constraints can perform better than others because they can be sent to specific nodes.

- **5.b)** SELECT query that has WHERE clause conditions which also restrict some of the primary key columns (in addition to the partition key columns) can perform even better.

- **5.c)** SELECT query can be invoked on Materialized View instead of base table. Such queries can also benefit if partition key of MV is restricted along with any of the other primary key columns.

- **5.d)** SELECT query which does not restrict all of the partition key columns will perform the worst, as the query has to be executed on all nodes that could potentially contain the answer.

- **5.e)** SELECT query that could potentially involve scanning a lot of data has to be marked ALLOW FILTER (just as in Apache Cassandra)

- **5.f)** Occasionally due to networking issues or other causes the SELECT query may timeout or have partial results. In such cases, a flag is set on the query results which can determine if the results were partial or had timed out. Sometimes a timedout query returns errors of the form "broken packet" to indicate that only partial results were obtain by timeout.

  NOTE: Depending upon the use case MV can perform better or Secondary index can perform better. Examine the where clause of your queries to determine which is better.
  - **5.f.1)** If your query restricts all columns of partition key and secondary index column then the query will be sent to a single node and will be fast enough.

  - **5.f.2)** If the query does not restrict all columns of partition key but restricts secondary index column then it will have to be sent to all nodes that could possibly contain the result, this will make it slower (but still likely much better than Apache Cassandra).

  - **5.f.3)** If the query restricts columns in a different order than the clustering order of columns in the base table, then an MV could perform better.

  - **5.f.4)** SELECT on MV has to be explicitly invoked on the MV (not on the base table).

  - **5.f.5)** **Important**: MV as well as Secondary indexes take up a lot of space, utilize them after carefully examining your queries. The more secondary indexes and MVs you create on a single table the more it will degrade the write performance. (This is true of all other databases as well).

- **5.g)** Since the leader has a lot of responsibilities, if enough vCPUs are not available (such as on weaker AWS Ec2 instances), its read load will be reduced so that member servers execute somewhat greater share of the read queries.
  
- **5.h)** By default the read queries are distributed amongst the nodes in the cluster based upon load balancing. They do not favor the node which is further ahead in applying the writes. This allows uniform resouce utilization in the cluster and provides better overall throughput. If we provide a separate flag to specify to obtain results from the nodes that are further ahead it will have some implications. It will make the load distribution imbalanced (and very poor resource utilization for reader threads on some nodes) and it is still not guaranteed that the latest commits will be visible. This is due to the nature of snapshot isolation which is explained below.

## 6. Database Transactions (read-only and read-write)
Transactions can be issued with a single query (Insert,Delete,Update or Select) or a batch of queries. Batch of write queries must contain only write queries (no Selects) and Batch of reads must contain only Selects.
- **6.a)** Batch of writes
  - **6.a.1)** Can contain a sequence of only insert, delete, update queries.

  - **6.a.2)** **Important**: If one of the writes in the batch fails to apply, it will be skipped and the user will be informed that it was skipped, however, the rest of the batch will be applied. This is not ideal. However, this is a first step that is likely to meet several use cases.

  - **6.a.3)** At the moment, the write batch query string, including space for query parameters and all other encodings should not be more than 320KB in size.

  - **6.a.4)** At the moment, range deletes or range updates are not allowed in a batch of writes, in other words, a delete/update query which is part of a batch of writes must specify all the primary key members in the Where clause to match a single object.

  - **6.a.5)** The user can specify range DELETE/UPDATE as a separate write txn (NOT as part of a write batch). The functionality is available in a limited form as described above.

  - **6.a.6)** With the exception of unappliable writes being skipped, the batch will be applied as a whole (in other words readers will not see partial results of a batch).

- **6.b)** Batch of read-only transactions - transactions that perform only reads. Currently, such batches which have even one query that has ALLOW-FILTER will not return results that are transactional (We will improve this later). Otherwise the transactions will return MVCC (multi-version concurrency control) consistent results. (MVCC is the same as snapshot isolation). MVCC read-only transactions return snapshot isolation consistent results, which can return data which is older than the most recently committed.

- **6.c)** The user can check on the SELECT query results if it was transactional or not.

- **6.d)** Schema/Security-data queries cannot be part of a batch.

- **6.e)** Write transactions utilize distributed consensus (raft consensus protocol), as a result, the database will become unavailable for writes if the majority of the coordinator nodes are down. This is the price to pay for consistency. Some of this is mitigated with a larger number of coordinator nodes.

- **6.f)** Writes will wait for responses from all involved servers before responding to the client. Failure/restart of 1 member will be tolerated, in the sense that the throughput will be reduced and writes will still be applied. However if multiple members fail/restart simultaneously then writes will stall untill the members are brought back online. We can later on improve upon this restriction. With a typical replication factor of 3, this is probably the safer approach. It also means that a slower node will slow down write transactions involving that node. Writes are applied only if a quorum of coordinators have received the write txn and marked it as to-be-applied. Thus as soon as the write returns if you submit an MVCC read-only query to obtain the modified data right away you might get older data. That is just the nature of MVCC, it is not guaranteed to return the most recently committed data. This is true of all MVCC implementations.

- **6.g)** Transaction functionality is rather limited, this is because this is a first implementation. We will improve upon it when we get a chance.

#### 6.h) Query Timeout
If a DELETE/UPDATE/INSERT hits a timeout, it could be due to one of the following reasons:
- **6.h.1)** The server has a lot of load and cannot handle too much write traffic, then write load should be reduced. In such a case, write queries submitted to the coordinator should not be resubmitted because they have been journaled and the cluster will eventually apply them.

- **6.h.2)** The coordinator failed over and a new one got elected. The failover process could take a very short time, typically a few milliseconds. In this case, the write query sent to the coordinator may have to be resubmitted because it may have been lost.

- **6.h.3)** The DELETE/UPDATE query has been specified with a WHERE clause which did not have enough restrictions on it and caused the servers to scan large swaths of the database and eventually caused a timeout. In this case, it is better for the user to re-examine the WHERE clause and add as many restrictions as possible to make it perform in subsequent invocations. NOTE: Primary key column restrictions are useful only if provided in order of specification of primary key columns in the schema for the table. If one of the primary key columns is not restricted then any restrictions on the subsequent (in order of primary key) columns will not help much for performance but maybe usefull for correctness.

- **6.h.4)** Repeatedly submitting the same DELETE/UPDATE query without addressing point 6.h.3) can make the database unavailable for subsequent writes and very significantly degrade performance of reads.

If a SELECT query times out, the causes could again be one of 6.h.1), 6.h.2), 6.h.3). The only difference is that SELECT queries are never journaled and in general are lighter weight compared to writes. The suggestions to address any issues are similar to the ones in 6.h.1), 6.h.2), 6.h.3).

## 7. Partition Key
As with Apache Cassandra, choose the partition key very carefully.
- **7.a)** Partition key is utilized for consistent hashing to distribute data evenly in the nodes in the cluster and a poorly chosen partition key will create hot spots which cannot be fixed. Adding new nodes in the cluster will NOT fix hot spots.

- **7.b)** **Important**: Choose a partition key with high enough cardinality so that it can be distributed somewhat randomly in the cluster. This is true of the base table as well as MVs.

- **7.c)** **Important**: When choosing a partition key be aware that SELECT query performance will be best when partition key equality restrictions are included in the query. This allows the query to be sent to only one node in the cluster, instead of to a lot of nodes that could possibly contain the data. This is true of queries on the base table as well as any MVs as well as queries on Secondary Index. Thus choosing a partition key also directly impacts SELECT performance. This is true of all clustered products including Apache Cassandra. (DELETE/UPDATE that affect multiple objects must also specify the partition key in the WHERE clause).

- **7.d)** **Important**: Just as with Apache Cassandra, once partition key, primary key columns have been chosen for a table they cannot be altered. The table has to be dropped and re-created.

- **7.e)** Partition Key must also be chosen carefully for Materialized View (else it will generate hotspots for MV records). MV partition key must also be of high cardinality.

- **7.f)** It is possible to colocate (on the same cluster node) related records of different tables by utilizing the same partition key for those tables. Currently, this is the only way to guarantee colocation of records of different tables. This will make a batch of SELECT queries on multiple related tables faster. However, you have to once again be careful not to introduce any hotspots. We will improve upon this later.

- **7.g)** NOTE that range-delete, range-update require a WHERE clause which restricts all partition key columns (on the base table as well as MV tables) with equality constraints. This must also be kept in mind when choosing the partition key if your usage requires deleting/updating a range of objects. However, 7.b) is very critical and the cardinality of the partition key has to be high enough to allow uniform distribution in the cluster.

- **7.h)** If you require co-locating multiple records on the same partition then you can choose the same partition key for them, but please make certain that such co-location does not imbalance the cluster by placing too many objects in the same partition. The overall goal is to utilize a high cardinality partition key to distribute objects evenly in the partitions.

- **7.i)** Colocating related items utilizing the same partition key also allows batched writes among them to be more efficient. And as we improve capabilities of write batches this will allow more flexible writes (although this will be available in later releases).

## 8. Schema Changes
Try to avoid making too many schema changes in your application at the moment, because the current product has very limited space overhead for that. A schema that contains 500+ tables may not work at the moment. We have to address these limitations in later releases.

## 9. Client Refresh
Every time you Drop/Add a table, refresh the client. This is an expensive operation and should only be done when a table is added/dropped, never otherwise. Client examples provided include this step.

## 10. Replication Factor
The industry-standard replication factor of 3 is recommended. This means that the minimum cluster size has to be 3 nodes. The product will not work on fewer nodes. In your storage calculations include replication factor. If your data is 10TB, then with replication factor included overall storage requirement is 30TB. If we include SSD overheads, journaling overheads, versioning overhead, fragmentation overheads, any background tasks merging overheads, etc., then it is safe to have overall storage of about 60TB available through the cluster. Other horizontally scalable databases have similar space overheads.

At the moment, the replication factor should not be less than 3. We will work later to get a better idea of how much of a risk there is to allow a replication factor of a minimum of 2. We did not consider this as it could potentially lead to data loss in an environment where there is no backup restore.

## 11. Co-ordinator Nodes
Coordinators (including the leader) nodes receive all the transaction journals. One of the co-ordinartor nodes is chosen the leader. The leader is responsible for transaction processing journal and for distributing read query load. The clients send their transaction processing requests to the leader. If the leader fails over the clients will detect it and attempt to contact other co-ordinators for query processing. Odd number of co-ordinator nodes should be chosen. The number of Coordinator nodes should be at least 3 but it can be more. The only downside to having more coordinator nodes is that it will increase the journal replication load by a little bit. But it should not matter too much. Choose enough to tolerate failures. At the moment the number of coordinators is chosen when creating a cluster and thereafter it cannot be altered.

## 12. Durability
At the moment the product relies on k-safety. This implies that as long as more than k/2 coordinator nodes are alive then the committed transactions are durable. Otherwise, we can lose some of the most recently committed transactions. (product restart alone should not cause logs to be lost, only power cycle or segfaults, etc. could cause this on a given node, so this should be quite rare). If you are concerned about this and want to tolerate more failures, please increase the number of coordinators to 5 or 7 depending on how big your cluster is. (k = number of coordinator nodes).

- **12.a)** If more than k/2 coordinator nodes are down, write txns cannot be applied and will timeout.

- **12.b)** If non-coordinator nodes which are involved in writes are also down then it can delay the application of transactions.

- **12.c)** It is best to monitor the cluster closely and replace failed AWS nodes (failure of AWS EC2 Instances can occur and should be planned for) as quickly as possible.

- **12.d)** This product chooses consistency over availability for write transactions in the presence of failures.

- **12.e)** During a failover to elect a new leader, pending write transactions that have been committed to the journal but not yet applied will still be applied. However, the client that originated the query will only receive a timeout and will reconnect to a different leader. In this situation, if the client resubmits the same write, then depending on the situation, the client will receive errors. Tests provided explain how to handle such a situation.

- **12.f)** As indicated below, utilize AWS partition placement groups to mitigate correlated failures of multiple AWS instances.

## 13. Cluster Size Limit
The current cluster size limit is 64 nodes. Although we have not stress tested clusters near 64 nodes yet to determine exact performance characteristics. We will improve upon this later.

## 14. Adding Nodes to the Cluster
When adding more nodes to the cluster, plan on adding at least 3 or 4 nodes (one by one, of course) to evenly spread out the data. However, you have to add the nodes one by one. You can check its rough status periodically and even cancel the ongoing operation if you change your mind, although the further along you are, the more work is needed to cancel the operation, and it is better to avoid canceling. Also, add nodes to a cluster only after it has sufficient data (i.e., nodes are all atleast 25% (the more the better) full to their maximum capacity, else the product may not be able to come up with a good data redistribution).

## 15. Replacing a Failed Node
When replacing a failed node, always utilize the node SUBSTITUTE command from the node tool. **DO NOT** utilize REMOVE node and then ADD node. SUBSTITUTE does not alter the data partitioning or ownership and will complete relatively faster and retain data distribution. REMOVE and ADD will cause a data repartitioning storm. This is very **Important** to understand and avoid. At the moment, we do not allow nodes of different AWS types in a server cluster. ___Note:___ Unlike Add/Remove it is possible to subtitute a node in presence of multiple failed nodes so long as the nodes that are alive have the data needed by the node being subtituted.

## 16. Removing Nodes from the Cluster
**Avoid** removing a node from the cluster (especially for replacement and even otherwise). This is because removing nodes can sometimes lead to poorer data distribution. This is a known issue, which we will resolve. For now, know that it will work, but it should be avoided as much as possible. Do it only if you know that you are going to shrink your database. This should be an uncommon situation. Removing a node from a cluster causes data to be
transfered to remaining nodes in the cluster and will severely degrade their performance for the duration of the removal operation. This is more of an issue on smaller AWS instances which have fewer vCPUs than the larger AWS instances. This is a known issue we have not gotten around to fixing it. So its preferable to remove nodes during times of low activity.

## 17. Datacenter Clusters
Currently, a single datacenter cluster is supported. However, the user can utilize AWS CloudFormation templates to replicate to multiple independent datacenters which are managed independently. The multiple datacenters in Apache Cassandra are also considered independent. In other words multi-datacenter replication is currently determined by the user, the product does not provide an option for this.

## 18. Monitoring Nodes
Check the liveness of nodes every few minutes and storage utilization of nodes every hour. If storage utilization is reaching the limits, try to add a few nodes to the cluster. Also, make sure that the data is more or less uniformly distributed in the cluster. Currently, adding nodes to a cluster requires user intervention; the cluster will not resize automatically. Failed nodes also need to be identified by the user and substituted. **NOTE**: Liveness of nodes should be checked more frequently, every 2 minutes. A single AWS instance of the very cheapest kind, such as t2.nano, can be dedicated to checking for utilization, liveness, and if there is a problem, send an alert to the administrator.

## 19. Interference with Database Servers
Please **DO NOT** directly interfere with database servers. Try to utilize only the APIs and tools provided. This is because there are workarounds for known issues in place, which if broken can cause the database to be unrecoverable, and you will have no option but to substitute the node in the cluster, which is going to waste too much time and resources sending all the data back and catching up. If you accidentally modify or delete configuration files on the server nodes, you are on your own, we will not be able to help you with it. In order for support to figure out issues we expect a baseline behavior that shouldn't be tempered with.

## 20. AWS Instance Types for Database Servers, Clients
Currently, our product works only with i3 and i3en AWS instances for database servers. AWS Spot instances should **NOT** be utilized with production clusters because the i3 instances (which are required for performance) will lose their database if the instance is stopped or terminated. For experimental clusters, spot instances can be utilized. AWS recommends i3 instances (or similar) with local SSDs for storage, for running databases such as Cassandra/DynamoDB. Other instances **DO NOT** perform well in some usage scenarios. Additionally, if you were to utilize Spot instances together with EBS storage, the storage costs would also add up quickly (you have to make a detailed calculation to figure out if there would be a significant difference in the two approaches for your use case. This is just for your knowledge; we **DO NOT** support EBS usage due to its poor performance for the usage scenarios of our product).

- **20.a)** Server instances are either i3 instances or i3en instances, and the cluster is composed of the same kind of instances so all nodes have roughly the same capability. For now, this is a requirement; later we can change this. In production, server instances have to be either reserved or on-demand. They cannot be SPOT instances. Additionally, we have also observed difficulty obtaining Spot instances sometimes; this may be specific to our usage, your experience might be different. (If you want to utilize Spot instances in production, you have to somehow guarantee that the instances will not get stopped/terminated because that will lead to data loss, and additionally, with enough such stopped instances, the product will stop working. We don't know if there is some way to guarantee this in AWS.).

- **20.b)** i3en instances are slightly cheaper by storage, but they have much lower performing SSDs, while they have faster network I/O and newer Xeon processors compared to i3 instances. They have better compute performance but lower SSD performance. So CPU-bound loads will be better, and I/O-bound loads will perform probably worse. There is also not a 1:1 mapping between i3 and i3en instances in terms of vCPU, memory. Evaluate your tradeoffs and also determine if these instances are available in enough numbers in your region.

- **20.b.1)** We do not yet support Graviton-based r6gd instances for servers because their advertised SSD I/O characteristics are much worse than i3 instances. As a result, for data sets that do not fit in memory, they could perform much worse. If customers want this option, we can look into it at a later point. If your workload is more CPU-bound, these instances could help; we will have to check later if there is demand for these.

- **20.c)** Client instances can be of one of the following AWS instance types:
    - m5.large, m5n.large, m5a.large, m5ad.large,
    - c5.large, c5n.large, c5a.large, c5ad.large,
    - m5.xlarge, m5n.xlarge, m5a.xlarge, m5ad.xlarge,
    - c5.xlarge, c5n.xlarge, c5a.xlarge, c5ad.xlarge,
    - m5.2xlarge, m5n.2xlarge, m5a.2xlarge, m5ad.2xlarge,
    - c5.2xlarge, c5n.2xlarge, c5a.2xlarge, c5ad.2xlarge,
    - m5.4xlarge, m5n.4xlarge, m5a.4xlarge, m5ad.4xlarge,
    - c5.4xlarge, c5n.4xlarge, c5a.4xlarge, c5ad.4xlarge,
    - m5.8xlarge, m5n.8xlarge, m5a.8xlarge, m5ad.8xlarge,
    - c5.9xlarge, c5n.9xlarge, c5a.8xlarge, c5ad.8xlarge,
    - i3.large, i3.xlarge

  The above are the only kinds of instances supported for clients. Clients do not store any data and they can be SPOT instances if your application can tolerate instance stop/terminate. If you plan to run a lot of client threads, utilize an instance with a lot of cores. Carefully examine the tradeoffs of the different types before choosing a client type. c5n instances have the highest network I/O. You could utilize Spot instances but plan for Client instance termination/interruption.

- **20.c.1)** NOTE: Unlike server clusters, the client clusters can be augmented to become heterogeneous. In other words, a single client cluster may be composed of instances of different AWS instance types. This is not the case with the server cluster, which has to be homogeneous for now.

- **20.d)** For experimentation with your databases, you can try utilizing SPOT instances for servers. This could work for testing purposes to reduce costs. But this is not recommended/supported for production servers. This is because spot instances do not guarantee availability. The user would have to actively monitor stopped/terminated instances and replace them. For small enough a database, this might be doable manually. If there are enough stopped instances, the product might fail in unexpected ways. If you have a way to guarantee that your instances are not going to be stopped/terminated, then you might be able to utilize spot instances for even larger database instances. If you have decided to utilize the product, then you can purchase annual AWS reserved instances to attain discounts on AWS costs as well as any software license costs. Note: If a spot instance gets terminated in the server stack, it will probably be better to simply delete the stack and start over because there is a chance that more instances might get terminated in the near time frame (possibly).

## 21. CloudFormation Effectiveness
Utilize the steps indicated in the link below to utilize AWS CloudFormation effectively (do switch on Logging AWS CloudFormation with AWS CloudTrail, also set up endpoints to ensure latency expectations):
Setting up with AWS CloudFormation

The steps to create/delete/modify/describe server/client clusters are documented separately. Please follow the directions carefully, and you should have the clusters running with example tests running in a short period of time.

## 22. Backup, Restore
We do not have backup/restore at the moment. The Cluster has a built-in replication factor, which has some fault tolerance. Utilizing AWS Partition Placement Groups can reduce the probability of correlated hardware failures. If there are multiple concurrent hardware failures, it can lead to data loss if all copies of a partition are lost due to the failures. In production clusters, utilizing Partition placement Groups is a requirement for this reason. However, note that utilizing Partition Placement Groups does not reduce the probability of correlated failures to zero. AWS also indicates that Partition Placement algorithms are "best effort" and depend upon the availability of instances in the partitions to keep the EC2 instances balanced across partitions. Thus, it is still possible to have correlated failures, although the probability of this happening should be reduced by some degree. It is possible that in some cases it reduces odds of correlated failures very significantly so that in practice data loss does not occur.

Also note that utilizing Partition Placement Groups is likely to degrade the performance of the servers to some degree, due to the fact that the servers are no longer as closely physically located.

(Backup/Restore can also be useful in situations of accidental deletion of data by the user. For accidental user deletion of data, we do not have any built-in mechanism to prevent/revert it. This can be mitigated via customer's application-level strategies).

AWS provides utilities to backup AWS instances, however, we have not yet investigated them and even if some functionality exists then we may still have to provide some tools to make it work with our product.

There are several third-party products that can be utilized for Backup/Restore. We have not investigated those, and there is a chance that we may have to provide some tools to make them work with our product.

NOTE: It's virtually certain that eventually one or more AWS instances will incur hardware failures and they will have to be replaced. Depending upon your usage, you have to determine carefully which approaches (if any) you want to utilize to mitigate a scenario where enough number of concurrent failures lead to data loss. Currently, such approaches would have to be determined at a layer outside our product.

We have to provide backup and restore tools as soon as we can.

## 23. Deployment Strategy
We utilize AWS CloudFormation templates to create AWS stacks to deploy our database clusters. You have to create an S3 bucket with write permissions in which the CloudFormation templates will be stored. All AWS EC2 instances must be within the same Subnet, VPC.

- **23.a)** As a first step, create a test cluster with the cheapest instances (i3.2xlarge for server clusters). If you can allocate Spot instances while guaranteeing somehow they will not be disrupted, then utilize Spot instances. They are not supported for production because disruptions can lead to loss of data, however, for testing purposes as long as you know you won't be disrupted, it will be more cost-effective to utilize this option. **Important**: Occasionally you will find that an AWS EC2 Spot instance is stopped. Please keep an eye on the AWS console to look out for this. If this is a test cluster, you could simply delete the clusters and start over. A stopped instance is considered to be a failed instance which would have to be substituted in a production cluster.
  - **23.a.1)** Utilize the computation below to determine, based upon the number of nodes in the cluster, how much data you can store in the database; choose a cluster of at least 4 nodes.

  - **23.a.2)** Drive the data load and validate that there are no hot spots and data is evenly distributed in the cluster. If data is not evenly distributed, rethink your tables, partition keys, drop the tables that are not evenly distributed. It might take some time for table drop to complete. And add back the tables with different partition keys. Make sure data is evenly distributed in the cluster. As long as partition keys are of high cardinality, there shouldn't be any hot spots.

  - **23.a.3)** When enough data has been added into the cluster, try adding 2 nodes in the cluster (one by one, of course) and validate that the data on the nodes is still more or less uniformly distributed.

  - **23.a.4)** Test all your usage queries to make sure everything works. This is crucial to avoid any surprises or bugs in production. This is extremely important at this stage of our product (BETA release), and we expect at least some hiccups.

  - **23.a.5)** Storage utilization on nodes in the cluster can be obtained with the commands provided.

If you are able to drive enough stress into the system and it presents no significant issues, you can utilize a production cluster consisting of more powerful EC2 instances. Thoroughly testing your use case scenario will minimize any risks in deployment.

- **23.b)** Once the use case has been validated, determine how much data you plan on having in the cluster. According to the above calculations, with a replication factor of 3 and accounting for SSD overhead, journaling, multiversioning overhead, fragmentation, background tasks, merging, etc., another factor of 2 has to be included. So for 10TB actual data, you need 10*3*2 = 60TB total storage requirement. While this might seem excessive, other horizontally scalable databases with replication factors have similar overheads.

- **23.c)** Try to utilize a cluster of less than 64 nodes, although we have not stress tested clusters nearing this size, so you may run into performance issues even at clusters of less than this size. We plan to address these issues soon.

- **23.d)** For different storage requirements, here are suggestions:
  - **23.d.1)** For a total storage requirement of < 122TB, utilize i3.2xlarge instances to form a cluster. (each i3.2xlarge instance has about 1.9TB of SSD available)

  - **23.d.2)** For a total storage requirement of < 244TB, utilize i3.4xlarge instances to form a cluster. (each i3.4xlarge instance has about 3.8TB SSD available)

  - **23.d.3)** For a total storage requirement of less than 488TB, utilize i3.8xlarge instances to form a cluster. Each i3.8xlarge instance has about 7.6TB of SSD available.

  - **23.d.4)** You can make similar calculations for other instances based on a cluster that could at most have 64 nodes in it. Multiply the storage available with the most needed instances (which could be 64). Note that these suggestions are only from storage point of view, not performance. Depending upon performance requirements larger AWS instances may be required.

  - **23.d.5)** The instances are priced linearly by capacity at AWS. Please double-check this. So from a cost perspective, for the same amount of data, a smaller cluster of beefier instances should cost similar to a larger cluster of smaller instances.

  - **23.d.6)** If you anticipate your storage requirements to be at least 6TB (meaning actual data is about 1TB), then it's best to utilize i3.4xlarge instances onwards. Weaker instances will not be cost-effective and will perform worse. Due to linear AWS pricing of i3 instances, you will not save anything by utilizing weaker instances (but please double-check this in your calculations, we are not responsible for any differing calculations).

  - **23.d.7)** Notice that the above calculations are with respect to storage. However, the smaller instances may also offer lower performance. So if higher performance matters to you even more than storage, you could utilize the larger i3.<n>xlarge instances even though your dataset size on the whole is not large enough at the moment.

  - **23.d.8)** When utilizing larger clusters, it might be better to utilize i3.4xlarge instances or larger AWS instances as they have more vCPUs available to handle more load.

**NOTE**: We are currently working on database data compression, that should hopefully improve upon the disk utilization indicated above.

- **23.e)** At the moment, each server cluster must have homogeneous instances. The instances in a cluster have to be of the same capacity or basically the same EC2 instance kind. We are aware of this limitation and we will improve upon this. This is just to keep things simple, and it is the case we have tested.
- **23.f)** Please be careful about which instances you utilize because the region in which you create the cluster may have different availabilities of different instances. Thus, it will be better to be on top of your storage utilizations on the instances to make sure that you add additional nodes well before hitting the storage limit on any one node. Otherwise, you will stall your cluster.
- **23.g)** Due to the replication factor of 3, at least 3 nodes have to be present in the cluster for the product to work. However, in that configuration, each node has identical data. It is best to choose an initial cluster size that is representative of how much data you plan to handle. This is because adding nodes to a running cluster can be time-consuming. It is also suggested to add at least 3 nodes (one by one) when adding nodes, as this will produce better-balanced clusters. Sometimes adding an additional node can take care of slight imbalances in the cluster.

- **23.h)** **Important**: Utilizing a lot of client threads will improve the overall throughput of the system. This is true of other databases such as Cassandra as well. The larger the server instances and the larger the cluster, the more the number of client threads that are needed to drive them to attain enough throughput. Write queries have different latency behavior compared to read-only queries, so it may be better to separate the client threads into readers and writers.

- **23.i.1)** Client machines do not store data, so they can be AWS Spot instances.
- **23.i.2)** At the moment, a single client process on a client machine has all the threads that communicate with the server cluster. Multiple client processes must not be created on a single AWS client. This is thought to be easier to manage. If users want to change this, then we can alter this later on, but for the moment we allow only one client process to contain all the threads that communicate with the server cluster. If you try to launch multiple client processes on a single AWS client, the results are not defined, most likely segfaults and failures to even start.
- **23.i.3)** If you cannot find instances in a given AWS region, you could try creating all your stacks in a different AWS region. The query processing within the stacks should not be impacted; however, latency will be a factor for the round trip of queries between the user and instances. (you may have to change the AWS_DEFAULT_REGION bash environment variable to the different regions for the python scripts to work).
- **23.i.4)** When utilizing Spot instances for clients, utilize more instances of slightly lower capacity to allow for instance termination. Having very few clients with a large capacity will impact service much more if a Spot instance were to be terminated.
- **23.i.5)** You can launch multiple client AWS EC2 instances (client cluster) to drive the load on the cluster.
- **23.i.6)** By default, a client will launch a total of 8 x EC2-instance-vCPUs threads/fibers.

- **23.j)** **Important**: It is recommended that when performing any cluster create/delete/add node/replace node, etc., that you keep an eye on the AWS CloudFormation console to make sure that the operation in AWS is succeeding. There can be failures in CloudFormation tasks due to reasons beyond our control, and you might have to occasionally clean up any partial state using CloudFormation console tools. (Other than deleting whole stacks (which are stuck due to bugs), AWS advises making updates to stacks using root stack template updates, rather than directly modifying nested stacks or any instance resources belonging to those stacks, otherwise, it can cause problems). Please utilize the tools provided to manage the AWS resources. DO NOT try to delete individual EC2 instances using AWS commands. It WILL cause problems with the running product.

- **23.k)** **Important**: If your instances are unable to communicate with each other, it may be that your AWS Security Group permissions do not allow communication between instances over private addresses. To determine if this is the case, try pinging one EC2 instance from another. If you cannot, then you have to address the Security Group permissions. Set up a traffic rule from within the security group for all ICMP IPV4 traffic within the group.

- **23.l)** **Caution**: We are not responsible for the accuracy of capacity, pricing, or any other information regarding AWS in this note. The customer must make their own validations and ensure that any planning they make is with respect to the latest AWS suggestions/requirements, etc. We will not be responsible for any issues that arise due to this. We have tried to make the information accurate to the best we know, but we could be mistaken, or the information may later change on AWS, and the user must confirm with their own calculations; we will not accept any liability for this.

- **23.m)** To monitor the status of the cluster, the cheapest instance can be reserved. All it has to be able to do is obtain the status of the cluster via TCP/IP and run peachydb_status_tool.py for it. It is recommended that such a status check be run every minute. And the user should set up a script to make sure that no instance is down. If some instance is down, then report that to the Administrator to take appropriate action. The user should also track storage utilization via this tool to ensure that the cluster is not imbalanced or reaching capacity. (you can utilize an AWS t3.nano spot instance for such a task).

- **23.n)** With reference to 23.m) it is important to understand that occasionally some nodes drop messages and are trying to catchup and will show up as offline. But this should be infrequent and action needs to be taken only if the node does not come back online on its own.

- **23.o)** As a first step write a simple test with all the schema and queries with variables bound to the queries. Make sure that there are no syntax errors and variable are bound correctly to the right parameters/positions. Once a simple test works then start to utilize stress test to load data and perform queries.

- **23.p)** The tools to utilize for deployment of Server and Client Stacks are located <a href="https://github.com/akseg73/PeachyDB/tree/main/Deployment" title="Documentation">here</a>. Examples of client drivers are located <a href="https://github.com/akseg73/PeachyDB/tree/main/examples" title="Documentation"> here</a>. Database Server Performance monitoring is explained <a href="https://github.com/akseg73/PeachyDB/blob/main/PerformanceMonitoring.md" title="perfmon"> here </a>.

## 24. Known Bugs, Issues
Below are known Bugs and Issues, we are not going to keep this list updated, but rather rely on other means (such as Github open issues (which we have recently started utilizing for tracking bugs), etc) to record new issues.

- **24.1)** If multiple nodes are dead or cannot communicate with the leader, then the leader will not be able to perform some GC tasks and write performance will severely degrade. Currently, a single node failure is tolerated but multiple simultaneous failures will cause write timeouts. It is prudent for the user to set up monitoring to check the status of the cluster every few minutes to ensure everything is working.

- **24.2)** Even if a single node fails, we will reduce write throughput to allow that node to join quickly once it comes back online. If the node is too far behind in the journals, we have no option but to snapshot the database again, which is not a very desirable possibility for a large database.

- **24.3)** When a node is being added or substituted in a cluster, the write throughput will be reduced to allow such nodes to catch up with the leader when they come online. If we do not reduce write throughput, by the time a large database has been snapshotted, it will already be too far behind compared to available logs and we will not be able to join the cluster. Thus, adding or substituting nodes is preferably done during times of lower activity.

- **24.4)** Issues related to Materialized Views are described in 3.f.1 and 3.f.2.

- **24.5)** Currently, all AWS EC2 instances (servers as well as clients) must be on the same Subnet and same VPC. In the AWS console, please ensure that the Subnet allows communication with all instances within the subnet, VPC. This is a requirement; otherwise, the product will not work. We will improve upon this limitation later.

- **24.6)** Currently, all server AWS EC2 instances have to be of the same AWS instance type.

- **24.7)** Currently, nodes can be added, removed, or substituted from the cluster only one at a time.

- **24.8)** Before adding more nodes to a cluster, make sure that the existing cluster at least has some representative data to determine how to repartition data. Otherwise, if you add nodes to a cluster with no data, it may not be able to make an effective partitioning decision and generate an imbalanced cluster. Instead of adding nodes to a cluster with no data, simply delete the cluster and recreate it with more nodes.

- **24.9)** As mentioned before, if you want to replace an instance, always utilize the Substitute command. Do not utilize (Remove node + Add node). Using Remove + Add can cause clusters to become imbalanced. We discourage users from utilizing the Remove node command as much as possible. It is utilized only in the rare circumstance of the user attempting to downscale a cluster if the cluster is very underutilized and they do not expect its utilization to grow.

- **24.10)** At the moment, the number of coordinator nodes is fixed at the time of cluster creation. It cannot be changed later on. We have to address this limitation. For larger clusters, it is better to have at least 5 or 7 coordinator nodes. For very small clusters, 3 coordinator nodes should be fine.

- **24.11)** In order for an add/remove/substitute operation to complete, all nodes have to be online. This may be a little too restrictive; we will have to improve upon this. Please keep an eye on cluster status during this time and make sure all nodes are up.

- **24.12)** The cluster_tool should NOT be utilized by multiple users simultaneously. It is most likely going to fail.

- **24.13)** If the client receives a timeout on a write query, this does not indicate that the query did not get executed. This might indicate some other problem. It may be that the leader failed over or the network was bad. If the client reissues the write query, it may get applied a second time and result in errors. If the write transaction is not idempotent and it is important to the user not to execute the write a second time, then they may have to issue a read query to determine if the write succeeded. However, note that due to the MVCC nature of read-only queries, it is possible that the read returns older data. Client examples provided include code that explains this issue.

- **24.14)** Please do NOT make schema changes during adding/removing/substituting nodes in the cluster as this can significantly degrade the performance of the cluster configuration change. Making cluster configuration changes while adding a Materialized View is not allowed and vice-versa.

- **24.15)** Currently, all the instances in an AWS stack for peachydb server/client stack have to be either On-Demand instances or all have to be Spot Instances; there cannot be a mix and match. The choice made at the time of creation of the stack remains in effect until the stack is deleted. Whether or not an AWS stack has spot instances is decided independently for server and client stacks.

- **24.16)** We utilize AWS Placement Groups (PG) to avoid correlated AWS Hardware failures in production clusters.
  - **24.16.1)** AWS documentation states that occasionally this strategy may not be able to allocate an EC2 instance in a placement partition.

  - **24.16.2)** For experimental clusters, using placement groups is optional, but do it ONLY for experimental clusters and ONLY if you are unable to utilize placement groups. Even for experimental clusters, try to utilize placement groups as that is the test for how a production cluster will behave (using PGs is the default behavior).

  - **24.16.3)** For production clusters, AWS Placement Groups must ALWAYS be turned on. Otherwise, correlated multiple instance hardware failures can lead to data loss. According to AWS, hardware failures are quite common, and we must work with the expectation that hardware will fail.

  - **24.16.4)** The choice to utilize Placement Groups can only be made at the point of cluster creation; after that, it cannot be altered.

  - **24.16.5)** Even with placement groups utilized, the probability of correlated hardware failures will go down but cannot be reduced to zero. This is because AWS algorithms for evenly distributing instances among partitions are 'best effort'. It is still possible that hardware failures can lead to multiple instance failures which might lead to data loss, even though the odds of this happening should be reduced.

  - **24.16.6)** As suggested elsewhere, in the event of failure, do check the AWS console for anything else that may be wrong.

  - **24.16.7)** Due to the above, plan expanding clusters in advance; don't wait for the cluster capacity to reach the brink before attempting to expand the cluster. If you get an insufficient capacity error, you can try speaking to AWS to check that your AWS account limits have not been reached, retry the operation with fewer instances, wait a few minutes and retry the operation, or try a different availability zone.

  - **24.16.8)** Partition placement groups could possibly result in lower network throughput than Cluster placement groups. But we cannot utilize cluster placement groups because they do not offer correlated failure safety.

- **24.17)** If you delete the server stack to re-create it, you must also delete the client stack and recreate it because the client stack is still referring to the IP addresses of the original server stack. The client stack must always be created after the server stack has been successfully created.

- **24.18)** Please read 6.f) to understand the behavior of write commitment and MVCC read-only transactions.
- **24.19)** Try to avoid utilizing large primary keys (keys that are larger than 1k) as they can degrade performance significantly. Fields that are not contributing to the uniqueness of the record should be placed in the clustering columns, not the primary key, unless they are also a part of the partition key. If large primary keys are utilized, try to order the fields in a way that a small prefix of the keys is likely to be unique to significantly speed up queries. This should be done even for primary keys that are smaller.
- **24.20)** Occasionally when running scripts with AWS commands, you may encounter an error due to clock skew. This can be fixed with the following command:

   `sudo ntpdate pool.ntp.org`.
- **24.21)** Set the AWS region before utilizing any provided tools as follows (utilize your region name):
  
   `export AWS_DEFAULT_REGION=us-west-1`.
- **24.22)** If you issue a stack command and the tool reports that the stack is not known, this may be due to clock skew, you can try the suggestion in 24.20).
- **24.23)** Do not perform `yum update -y` on the EC2 server/client instances without having a product update patch applied. Applying `yum update -y` on instances can alter libraries and break dependencies, which can break the product functionality.
- **24.24)** As noted in points 24.1) and 24.2), server node failure degrades performance, so it is important to bring nodes back online as soon as possible.
- **24.25)** Occasionally core files could be created on the servers. If these are not due to a deterministic issue, you can remove them with provided commands. Otherwise, you can set up a support case and provide the support bundle which contains the cores.
- **24.26)** Each individual record in the database at the moment should be less than 1MB in size. This limitation can be removed if users require it.
- **24.27)** Please take note of point 3.f.2) above about Materialized Views.
- **24.28)** Please read point 22) above for another limitation.
- **24.29)** Sometimes error responses from query execution can be vague. This needs improvement.
- **24.30)** For larger instances like i3.8xlarge, i3.16xlarge, etc., please allow a few minutes for the server stack to initialize the large shared memory segments. Locking (mlock) large shared memory segments can take a long time.
- **24.31)** Please note point 19) above and do NOT interfere with server Ec2 Instances.
- **24.32)** If the parameters you provided failed to satisfy the CFT (Cloud Formation Template), you can check the templates `peachydb_template.json` and others in the AWS console.
- **24.33)** Range-selects, range-deletes, range-updates should be used sparingly as they will degrade performance depending on how much data is impacted. Similarly, each secondary index, MV added to a table will degrade the performance of writes proportionately.
- **24.34)** The per-partition limit on select queries currently does nothing and should not be utilized.
- **24.35)** In a large database whose size is much larger than available RAM, the read and write performance is limited by the I/O performance of the instance. Performance measurement should consider steady-state performance, where the database size has reached some point of stability.
- **24.36)** At the moment, the leader responds to a client on a write query when 'enough' number of member nodes have applied the write to cover all the 'partition keys' involved.
- **24.37)** Please try to avoid doing multiple large operations concurrently, such as altering cluster configuration by add/remove/substitute of nodes together with dropping a table, etc. Multiple resource-intensive operations can severely degrade performance.
- **24.38)** Larger clusters are better handled by larger AWS instances since they have more vCPUs to handle more load.
- **24.39)** Write queries have completely different latency behavior compared to read queries. It may be better to separate client threads that issue write queries from those that issue read queries.
- **24.40)** The number of client threads you can effectively utilize is also determined by whether your workload is primarily I/O bound or CPU bound.
- **24.41)** The performance monitoring section details several aspects of client performance tuning according to known issues.
- **24.42)** If your query performance has degraded, it is most likely due to some node not being online. One way to check this is to keep track of QPS, and if you see a significant degradation, check if the members are all online or if something else is going on with some node. Query timeouts can also happen when the leader node restarts, electing a new leader.

- **24.43)** Since we utilize murmur hash for partitioning, the `token()` function is meaningless in this usage. Therefore, for the current release, the `token()` function is not supported. If later on we allow other approaches to hashing, we can revisit this functionality.

- **24.44)** Currently, cluster configuration changes (add/remove/substitute nodes) must be initiated only when all nodes are online. If nodes go offline during cluster configuration changes, the operation could suffer performance loss. Additionally, completing the operation might require the nodes involved to be online. It is best to keep an eye on the cluster to ensure that everything is working. For node addition, all existing nodes must be online. For node substitution, the node being substituted can be offline, but the remaining must be online. For node removal, all nodes must be online.

- **24.45)** Please take a look at 23.i.2) and the example client provided to create the client.

- **24.46)** Please review point 12) above carefully to understand the durability of transactions.

- **24.47)** As noted in point 22) above, we do not have a backup restore at the moment.

- **24.48)** Continuous Integration/Continuous Deployment (CI/CD) in AWS is not yet supported. We have to work on this part to make the customer experience much more seamless.

- **24.49)** Occasionally AWS operations performed with scripts can fail; it's best to keep an eye on the AWS console to make sure that the operation went through as expected.

- **24.50)** When the database size exceeds the size of the Database Page Cache size, performance will degrade slightly, and when the database size exceeds RAM and SSD I/O is required, then performance will degrade further. This is true for all kinds of databases.

- **24.51)** In schema definition, any user-defined types must be defined before the tables that utilize them. User-defined type arguments are provided in the form of a collection of name-value pairs.

- **24.52)** Canceling an ongoing cluster configuration change (add/remove/substitute node) operation is not advised. It's available if the change was mistakenly started and needs to be canceled right away. It has not been tested very well to see if it works when the operation is well on its way. It does work when an operation has just about started.

- **24.53)** Before adding/removing/substituting a node in a cluster, please make sure that all the nodes in the cluster are live (with the exception of substitute). Otherwise, the operation will get stuck communicating with dead nodes.
- **24.54)** Immediately after a cluster configuration change (add/remove node), it is expected that performance will degrade temporarily because older vnodes are being purged from nodes where they are no longer needed. This task is delayed until the add/remove completes so that the data can still be served in the interim. If a cluster configuration change has just completed and some vnodes are to be removed from a node, then if an additional cluster configuration change is invoked which adds back some of those vnodes, it will be stalled until vnode_gc completes removal of those vnodes. This should only happen in node removal.

- **24.55)** Instead of specifying `uuid()`, you have to specify `fnuuid()` in queries when obtaining a UUID. Also, `uuid()` cannot be utilized as a part of the partition key; otherwise, you will not be able to query the table since the client has no track of UUID.

- **24.56)** All components of the partition key must be generated at the client and provided with the query; this appears to be a requirement in Cassandra as well.

- **24.57)** If an EC2 instance newly added to a cluster fails to come up, you can collect `/var/log/peachy.errors` from the instance to see if there are any problems.

- **24.58)** When looking at query statistics, it's important to understand that the statistics are printed every 10 minutes, which may suggest lower CPU utilization during periods of low/no activity. Care should be taken to look at statistics that are representative of durations when the clients are driving sufficient traffic to the servers.

- **24.59)** If an AWS EC2 Instance restarts (product restart/reboot), then for some time, the write performance on all the instances will degrade because the just restarted node will incur I/O to build the cache and will reduce the throughput in the cluster.

- **24.60)** Adding nodes to the server/client stack will retain the property `SpotInstance` utilized for creating the original stack. If the original stack was created with spot instances, the newly added instance will also be a spot instance.

- **24.61)** Occasionally, a cluster configuration change appears to be stuck, and AWS does add/remove/substitute the node, but the node refuses to come online or join the cluster. This can happen if some aspect of AWS infrastructure did not get set up in a timely manner. If "ps -elf |grep -i server" on the new node shows database server not running, errors in `/var/log/peachy.errors` can be correlated with `setup_cluster_config.py`. Repair can involve running a repair script.

- **24.62)** We have measured and confirmed with AWS an issue regarding ephemeral SSD performance attached to i3/i3en instances. The performance of I/O on the ephemeral storage can vary a lot depending on the other processes running in other instances associated with the same physical resources. This has been addressed to some extent in our layer.

- **24.63)** Complete the currently submitted operation on an AWS stack before submitting another one; otherwise, the behavior can be unpredictable. Any commands issued via the tools provided should not be prematurely terminated with Ctrl-C unless you know what you are doing.

## 25. Limits
As with all Databases we have size limits on numerous elements of the database. Some are listed below, its possible that we may have missed some important ones, if you notice any please let us know.

- **25.a)** The maximum query request size, including all serialized parameters, is 320kb. The larger the request, the more time-consuming it will be. Please also be mindfull of how many of these you want to submit at a time, the servers have RAM limits, a very large number of simultaneous large queries can potentially cause memory swapping on the server.

- **25.b)** If all the clients submit a combined more than 1k large queries at a given instant, then some of them may get rejected. A large query is any query which is serialized in more than 1300 bytes.

- **25.c)** Each collection (list, set, map) is limited in size to 64kb. This limit is imposed by the 2 byte offset representation for these structures. We can alleviate the issue if users request it.

- **25.d)** Currently, each record (primary key, clustering columns) should be less than 1mb. This is an artificial limitation related to replication, which we can later remove. We have not tested records that are that large; the actual limit may be a little smaller due to numerous buffer sizes utilized. This can be later ameliorated on customer request.
- **25.e)** By default, the maximum query response size returned by servers is 1mb.
- **25.f)** Any server cannot have more than 2k local read-only queries active at a time.
- **25.g)** Range deletes will delete no more than 1k objects at a time.
- **25.h)** A single write query batch cannot update more than 100 tables and materialized views combined. Likewise, a single read query batch cannot access more than 60 tables and materialized views. These are not 'unique' table numbers but the total number of table/MV accesses. Each query accessing a table/MV would count towards the total even if a previous query in the same batch has accessed the same table/MV.
- **25.i)** For batched writes, any error messages for the writes will be reported for only the first 10 failed writes in the batch; the rest of the writes will be accompanied by any error codes but not any error messages.
- **25.j)** There can be atmost 16k concurrent write transaction requests submitted by client fibers/threads.
- **25.k)** There is an upper limit of 64k on the sum total all the client-threads/fibers on all of the client instances taken together. We have not tested anywhere near that high number of client threads, so the actual figure may be a little lower.
- **25.l)** A single AWS client instance can run only one client driver, which usually has multiple threads and or multiple fibers.

## 26. Security Issues

- **26.a)** Currently, we do not have specific encryption (data at rest, in transit, or field-specific) related features or private networking, etc. We do not have any TLS/SSL-based client and inter-node communication. The current product represents a large attack surface for malicious actors. If this is important to your usage, please check if you have alternate mechanisms in place for cloud security which can provide you security guarantees. (Such issues exist with Cassandra as well, as of the last check. It's possible that there have been improvements since).
  
- **26.b)** We have Apache Cassandra specific role-based authentication.

## 27. Costs

New users will be offered a 1-month free trial offer. During this time, your AWS instances will not incur any software license charges. However, any underlying AWS infrastructure, instances, etc., will still incur AWS costs that will be charged by AWS. Once the 1-month trial period expires, you will be charged a minimal hourly software license fee per AWS instance, as listed in AWS marketplace listing, in addition to the AWS infrastructure costs. For significant discounts, an annual software license can be purchased. For customers with larger clusters we can try to work with you to make it even more cost effective. (For the initial 1 month it's possible to utilize a minimal experimental 3 node cluster of servers (i3.2xlarge) and client instances utilizing Spot Instances (if available) for a cost of less than $1/hour (based upon pricing of spot instances of these types currently (6/15/24) in west coast US region)).

Why do we charge a software license fee at all?

- **27.a)** We offer a product that should outperform and outscale Cassandra.

- **27.b)** We have a simplified consistency model to significantly reduce application development costs.

- **27.c)** We have virtually zero administration costs (excepting initial setup effort and minimal monitoring and cluster size adjustment actions).
  
- **27.d)** We avoid the severe pathologies related to data modifications that alternatives have and we also allow more options on write transactions.
  
- **27.e)** The products that do charge a software license fee are more expensive than ours and yet may be deficient in some of the features we provide.
  
- **27.f)** Please try other products and then compare for yourself; you will find our product to be easier to work with and have lower costs.
  
- **27.g)** This is just a start; we plan to make this much more useful, both in terms of performance and very large and significant features, and provide several additional associated products and technologies, and also address aforementioned limitations and community feature requests.
  
- **27.h)** How do we provide this service without having some way to fund the development?

## 28. Support and License

We have made every effort to ensure a product of extremely high quality but some initial hiccups are inevitable.

- **28.a)** Customers with support contracts can open support issue with Tech support.

- **28.b)** **Caution**: As indicated in our license, the product is provided as is and we are not responsible for any losses that may occur due to utilizing this product. We will not accept any form of litigation for any issues. To minimize your risk please test your use case thoroughly. You will likely find the product exceeds your expectations for the use cases it is designed for. Quality is of utmost importance to us and the product is very heavily stress tested over an elaborate period of time. It is important to carefully evaluate the limitations of this product, which have been described above as well as in the test provided, to avoid any surprises.

- **28.c)** Currently the product is released only on Amazon AWS and is subject to all costs associated with AWS. We will formulate a separate license for allowing users to run product on their private resources later on.

- **28.d)** We can not offer customer support to organizations that are providing support on this product to third parties.

- **28.e)** Please test your use case very thoroughly before purchasing any annual reserved instances to minimize any surprises along the way.

- **28.f)** If you have run into a critical bug, you can open an issue in Github, after checking that it is not a duplicate of a known issue. Depending upon availability of resources we can get to it.

- **28.g)** Since we are mostly compatible with Cassandra CQL, if you have issues with our current release you can migrate to Cassandra or similar products easily. We hope that this should not be needed.

## 29. Benchmarks

What about benchmarks?

- **29.a)** Benchmarks can be smoke and mirrors. All vendors contrive them to favor their product.

- **29.b)** Please make your own measurements for your use case.

- **29.c)** We expect to perform better than Cassandra.

- **29.d)** Subsequent releases will be more focused on performance improvements. Like other products, we have a very long list of improvements that should make us significantly better in every metric compared to our current release and competitive with the best products available out there in performance, latency, scalability and features. Some initial missed corner cases are inevitable, we will work with you on those.

- **29.e)** Note points 5.f.5) and 23.h) above, among others, for performance improvement. Note 6.f) for explanation of performance implications of distributed transaction. A slow node can slow down writes involving that node. In effect it will reduce throughput of the cluster. Database Server restart can have such an effect.

- **29.f)** Our product also has limitations listed in the sections above. If your use case fits better with the alternatives, it would be better to stick with them. Different products may be optimized better for different situations.

- **29.g)** Before criticizing/disparaging this product please be considerate of the fact that we are a very small team with very limited resources. However, as we obtain more resources, we expect to rapidly address majority of the issues and close the gap with the very best products. In the meantime any issues encountered should be recorded in Github issues for the project.

- **29.h)** Due to compatibility with Cassandra CQL you can easily migrate to other products if they fit your usage better.

## 30. Why are we closed source?

At the moment we want the user to focus only on their application and not depend upon implementation specifics of our product which could be significantly altered in subsequent releases.

We want to minimize the effort users have to undertake to interact with the product.

We may also have to weigh in the possibility of exposing code which can potentially become exposed to security exploits. AI models training on open source code may pose further issues.

Contributing to the open source as well as other community is an important part of our overall thought process, we have to figure out the details of how to give back to the ecosystem in a way that helps as many organizations and people as we can.

We have to revisit this later as we have a better idea on product positioning.
