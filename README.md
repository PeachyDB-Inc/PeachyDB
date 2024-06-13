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

### 6.h) QUERY TIMEOUT
If a DELETE/UPDATE/INSERT hits a timeout, it could be due to one of the following reasons:
- **6.h.1)** The server has a lot of load and cannot handle too much write traffic, then write load should be reduced. In such a case, write queries submitted to the coordinator should not be resubmitted because they have been journaled and the cluster will eventually apply them.
- **6.h.2)** The coordinator failed over and a new one got elected. The failover process could take a very short time, typically a few milliseconds. In this case, the write query sent to the coordinator may have to be resubmitted because it may have been lost.
- **6.h.3)** The DELETE/UPDATE query has been specified with a WHERE clause which did not have enough restrictions on it and caused the servers to scan large swaths of the database and eventually caused a timeout. In this case, it is better for the user to re-examine the WHERE clause and add as many restrictions as possible to make it perform in subsequent invocations. NOTE: Primary key column restrictions are useful only if provided in order of primary key columns. If one of the primary key columns is not restricted then any restrictions on the subsequent (in order of primary key) columns will not help much.
- **6.h.4)** Repeatedly submitting the same DELETE/UPDATE query without addressing point 6.h.3) can make the database unavailable for subsequent writes and very significantly degrade performance of reads.
- **6.h.5)** There is also a possibility that due to a software bug the coordinator is hanging indefinitely and it may never respond. In such a situation, we have to investigate the bug. Sometimes restarting the product can help to keep moving on, but sometimes it may not help.

If a SELECT query times out, the causes could again be one of 6.h.1), 6.h.2), 6.h.3). The only difference is that SELECT queries are never journaled. The suggestions to address any issues are similar to the ones in 6.h.1), 6.h.2), 6.h.3).

### 7. PARTITION KEY
As with Apache Cassandra, choose the partition key very carefully.
- **7.a)** Partition key is utilized for consistent hashing to distribute data evenly in the nodes in the cluster and a poorly chosen partition key will create hot spots which cannot be fixed. Adding new nodes in the cluster will NOT fix hot spots.
- **7.b)** IMPORTANT: Choose a partition key with high enough cardinality so that it can be distributed somewhat randomly in the cluster. This is true of the base table as well as MVs.
- **7.c)** IMPORTANT: When choosing a partition key be aware that SELECT query performance will be best when partition key equality restrictions are included in the query. This allows the query to be sent to only one node in the cluster, instead of to a lot of nodes that could possibly contain the data. This is true of queries on the base table as well as any MVs as well as queries on Secondary Index. Thus choosing a partition key also directly impacts SELECT performance. This is true of all clustered products including Apache Cassandra. (DELETE/UPDATE that affect multiple objects must also specify the partition key in the WHERE clause).
- **7.d)** IMPORTANT: Just as with Apache Cassandra, once partition key, primary key columns have been chosen for a table they cannot be altered. The table has to be dropped and re-created.
- **7.e)** Partition Key must also be chosen carefully for Materialized View (else it will generate hotspots for MV records). MV partition key must also be of high cardinality.
- **7.f)** It is possible to colocate (on the same cluster node) related records of different tables by utilizing the same partition key for those tables. Currently, this is the only way to guarantee colocation of records of different tables. This will make a batch of SELECT queries on multiple related tables faster. However, you have to once again be careful not to introduce any hotspots. We will improve upon this later.
- **7.g)** NOTE that range-delete, range-update require a WHERE clause which restricts all partition key columns (on the base table as well as MV tables) with equality constraints. This must also be kept in mind when choosing the partition key if your usage requires deleting/updating a range of objects. However, 7.b) is very critical and the cardinality of the partition key has to be high enough to allow uniform distribution in the cluster.
- **7.h)** If you require co-locating multiple records on the same partition then you can choose the same partition key for them, but please make certain that such co-location does not imbalance the cluster by placing too many objects in the same partition. The overall goal is to utilize a high cardinality partition key to distribute objects evenly in the partitions.
- **7.i)** Colocating related items utilizing the same partition key also allows batched writes among them to be more efficient. And as we improve capabilities of write batches this will allow more flexible writes (although this will be available in later releases).

### 8. Schema Changes
Try to avoid making too many schema changes in your application at the moment, because the current product has very limited space overhead for that. A schema that contains 500+ tables may not work at the moment.

### 9. Client Refresh
Every time you Drop/Add a table, refresh the client. This is an expensive operation and should only be done when a table is added/dropped, never otherwise.

### 10. Replication Factor
The industry-standard replication factor of 3 is recommended. This means that the minimum cluster size has to be 3 nodes. The product will not work on fewer nodes. In your storage calculations include replication factor. If your data is 10TB, then with replication factor included overall storage requirement is 30TB. If we include SSD overheads, journaling overheads, versioning overhead, fragmentation overheads, any background tasks merging overheads, etc., then it is safe to have overall storage of about 60TB available through the cluster. Other horizontally scalable databases have similar space overheads.

At the moment, the replication factor should not be less than 3. We will work later to get a better idea of how much of a risk there is to allow a replication factor of a minimum of 2. We did not consider this as it could potentially lead to data loss in an environment where there is no backup restore.

### 11. Co-ordinator Nodes
The number of Coordinator nodes should be at least 3 but it can be more. The only downside to having more coordinator nodes is that it will increase the journal replication load by a little bit. But it should not matter too much. Choose enough to tolerate failures. At the moment the number of coordinators is chosen when creating a cluster and thereafter it cannot be altered.

### 12. Durability
At the moment the product relies on k-safety. This implies that as long as more than k/2 coordinator nodes are alive then the committed transactions are durable. Otherwise, we can lose some most recently committed transactions. (product restart alone should not cause logs to be lost, only power cycle or segfaults, etc. could cause this on a given node, so this should be quite rare). If you are concerned about this and want to tolerate more failures, please increase the number of coordinators to 5 or 7 depending on how big your cluster is. (k = number of coordinator nodes).

- **12.a)** If more than k/2 coordinator nodes are down, write txns cannot be applied and will timeout.
- **12.b)** If non-coordinator nodes which are involved in writes are also down then it can delay the application of transactions.
- **12.c)** It is best to monitor the cluster closely and replace failed nodes as quickly as possible.
- **12.d)** This product chooses consistency over availability for write transactions in the presence of failures.
- **12.e)** During a failover to elect a new leader, pending write transactions that have been committed to the journal but not yet applied will still be applied. However, the client that originated the query will only receive a timeout and will reconnect to a different leader. In this situation, if the client resubmits the same write, then depending on the situation, the client will receive errors.
- **12.f)** As indicated below, utilize AWS partition placement groups to mitigate correlated failures of multiple AWS instances.

### 13. Cluster Size Limit
The current cluster size limit is 64 nodes. We will improve upon this later.

### 14. Adding Nodes to the Cluster
When adding more nodes to the cluster, plan on adding at least 3 or 4 nodes (one by one, of course) to evenly spread out the data. However, you have to add the nodes one by one. You will find this to be a much more convenient task than with Cassandra. You can check its rough status periodically and even cancel the ongoing operation if you change your mind, although the further along you are, the more work is needed to cancel the operation, and it is better to avoid canceling. Also, add nodes to a cluster only after it has sufficient data (i.e., nodes are all at least 50% full to their maximum capacity, else the product may not be able to come up with a good redistribution).

### 15. Replacing a Failed Node
When replacing a failed node, always utilize the node SUBSTITUTE command from the node tool. **DO NOT** utilize REMOVE node and then ADD node. SUBSTITUTE does not alter the data partitioning or ownership and will complete relatively faster and retain data distribution. REMOVE and ADD will cause a data repartitioning storm. This is very important to understand and avoid. When substituting a node, please utilize a node with the same capabilities as before (same storage and vCPUs), else the code can potentially get confused and generate imbalanced nodes. At the moment, we do not allow nodes of different AWS types in a cluster, so having dissimilar nodes in the cluster should not be much of a concern.

### 16. Removing Nodes from the Cluster
**DO NOT** remove a node from the cluster (especially for replacement and even otherwise). This is because removing nodes can sometimes lead to poorer data distribution. This is a known issue, which we will resolve. For now, know that it will work, but it should be avoided as much as possible. Do it only if you know that you are going to shrink your database. This should be an uncommon situation.

### 17. Datacenter Clusters
Currently, a single datacenter cluster is supported. However, the user can utilize AWS CloudFormation templates to replicate to multiple independent datacenters which are managed independently. The multiple datacenters in Apache Cassandra are also considered independent.

### 18. Monitoring Nodes
Check the liveness of nodes every few minutes and storage utilization of nodes every hour. If storage utilization is reaching the limits, try to add a few nodes to the cluster. Also, make sure that the data is more or less uniformly distributed in the cluster. Currently, adding nodes to a cluster requires user intervention; the cluster will not resize automatically. Failed nodes also need to be identified by the user and substituted. **NOTE**: Liveness of nodes should be checked more frequently, every 2 minutes. A single AWS instance of the very cheapest kind, such as t2.nano, can be dedicated to checking for utilization, liveness, and if there is a problem, send an alert to the administrator.

### 19. Interference with Server Containers
Please **DO NOT** directly interfere with server containers or attach to them. Try to utilize only the APIs and tools provided. This is because there are workarounds for known issues in place, which if broken can cause the database to be unrecoverable, and you will have no option but to rejoin the node in the cluster, which is going to waste too much time and resources sending all the data back and catching up. This will also degrade the performance of the cluster during the joining.

### 20. AWS Instance Types for Database Servers
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

- **20.c.1)** NOTE: Unlike server clusters, the client clusters can be augmented to become heterogeneous. In other words, a single client cluster may be composed of instances of different AWS instance types. This is not the case with the server cluster (which has to be

