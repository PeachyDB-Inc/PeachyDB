#### Frequently Asked Questions

---

<details>
  <summary>1. Write performance tanked, what happened?</summary>
  <br>
  Writes in this database are subject to the following constraints:<br>
  <ul>
    <li><b>1.a)</b> Each write will respond to the client only after all live servers which involve that write have responded to the leader node. Thus, a slow server node will slow down the writes that involve that server.</li><br>
    <li><b>1.b)</b> If a server has restarted/failed, then writes will be throttled significantly (as much as 70%) to allow the node to rejoin the cluster. Otherwise, so much write log will be accumulated that the server may not be able to catch up after restart. Thus, any failed node in the cluster should be brought back online as quickly as possible.</li><br>
    <li><b>1.c)</b> Writes are significantly faster for data that lies in the database cache. If the whole database fits in database cache writes will be much faster. If SSD i/o is involved for the data pages, writes will slow down by a significant amount. This is why during initial operation the database is much faster and once we hit the cache limit the performance degrades by some measure.</li><br>
    <li><b>1.d)</b> Cluster Configuration change (node add/delete/substitute) is ongoing. During this change, we have to deal with long-running read transactions which accumulate MVCC versions; to mitigate this, we throttle the writes significantly.</li><br>
    <li><b>1.e)</b> After each Cluster Configuration change, vnode_gc will attempt to garbage collect vnodes from instances from which their ownership has been removed. This requires scan of the database and possibly a lot of writes to remove those data items. Thus immediately after the Cluster configuration change, the write performance will significantly degrade till the vnodes have been garbage collected.</li><br>
    <li><b>1.f)</b> Adding/Dropping Secondary Indexes, Tables are also time consuming operations and will significantly degrade online write performance till the table_ops operation completes. It is best that such operations are undertaken at quiescent point, with low write activity.</li><br>
    <li><b>1.g)</b> Write performance is also impacted a little by utilizing Partition Placement groups. However, for production clusters, using Placement groups is required.</li><br>
    <li><b>1.h)</b> A small performance loss may also be incurred (if the database is small enough to fit in cache) due to requirements of the Raft consensus protocol.</li><br>
  </ul>
</details>

---

<details>
  <summary>2. The new node being added to the cluster appears to be hung, what should I do?</summary>
  <br>
  Follow these diagnostic steps:
  <ol>
    <li>SSH into the AWS instance.</li>
    <li>Navigate to the store directory: <code>cd /dbstore</code></li>
    <li>Check the message log: <code>tail -f mypeachdb/peachdb.messglog</code></li>
    <li>You can also look at <code>/var/log/peachy.errors</code>.</li>
    <li>Occasionally, it has been found that the ports could not be bound to DPDK and you will see the <code>peachdb.messglog</code> indicating so. There is an open GitHub issue #124 for this case.</li>
  </ol>
  If there is a definitive failure, then:
  <ul>
    <li>Cancel the cluster configuration change.</li>
    <li>Resubmit the change.</li>
  </ul>
  <br>  
</details>

---

<details>
  <summary>3. An AWS EC2 server stack instance died, what should I do?</summary>
  <br>
  If the failure is at AWS and the instance cannot be revived, you have to substitute the instance. In the presence of multiple corelated failures, if all servers responsible for a vnode have failed, you will experience data loss.
  <br><br>
</details>

---

<details>
  <summary>4. How do I obtain a support bundle?</summary>
  <br>
  The <code>peachydb_status_tool</code> can be utilized to obtain support bundle for servers in the server AWS CloudFormation Stack. This is explained in <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> monitor servers </a>.
  <br><br>
</details>

---

<details>
  <summary>5. How do I monitor the server cluster?</summary>
  <br>
  The <code>peachydb_status_tool</code> can be utilized to obtain liveness information of the servers in the server AWS CloudFormation Stack. This is explained in <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> monitor servers </a>.
  <br><br>
  If you find some server is hung, you can do the following:
  <ul>
    <li>5.a) Collect the support bundle and look at the logs: <code>peachdb.messglog*</code>, <code>perf.peachdb.messglog*</code>, <code>bulk.peachdb.messglog*</code></li>
    <li>5.b) Generally, it is best not to SSH into the server instances. However, if a node is not responsive, you suspect some kind of issue, and you want to make a quick check, you can:
      <ul>
        <li>SSH to the server</li>
        <li><code>cd /dbstore</code></li>
        <li><code>tail -f mypeachdb/peachdb.messglog</code></li>
      </ul>
    </li>
  </ul>
  <br>
</details>

---

<details>
  <summary>6. I get "mvccro garbage collected" or "stale handle" as response to a read query, what should I do?</summary>
  <br>
  This should, hopefully, not occur for cases other than long-running read transactions. Reads (short or long-running) that involve only a single partition key should not be seeing this error. 
  <br><br>
  If you believe you have short reads that are running into this issue, you can file an issue in GitHub. There is a chance that due to a race condition, an incorrect distributed <code>jnltxid</code> was assigned to a reader; this is a bug that has to be addressed. In this case we would like to look at the bulk.peachdb.messglog from the support bundle to look for the messages of the form "mvccro has been garbage collected", this will provide us more details on where the race condition may be.
  <br><br>If you entounter the error the read request has to be resubmitted.
  <br><br>
</details>

---

<details>
  <summary>7. How do I determine if clients or the servers are the bottleneck?</summary>
  <br>
  Perform <code>mpstat</code> on the client. You will most likely find an uneven distribution of load on the different cores.
  <br><br>
  However, if CPU utilization is low, then the server is likely the bottleneck. You can look at <code>mypeachdb/perf.peachdb.messglog</code> from the support bundle to examine the CPU utilization for reads/writes. The servers may be either CPU or I/O bound. The stats have to be interpreted correctly. 
  <br><br>
  You can also experiment with either increasing the number of client threads or using an additional client. If neither makes a difference, then the servers are bottlenecked on either CPU or I/O.
  <br><br>
</details>

---

<details>
  <summary>8. Why does mpstat on the client show uneven distribution of CPU utilization?</summary>
  <br>
  Some of this is due to variances of system calls, and some is due to the CPU core 0 being utilized by the OS. Therefore, the numerous cores are not uniformly available to the client.
  <br><br>
</details>

---

<details>
  <summary>9. A Spot instance in AWS was terminated, what should I do?</summary>
  <br>
  Spot instances for server AWS CloudFormation stacks are not recommended for production because they can be asynchronously terminated by AWS due to any reason. 
  <br><br>
  If you are utilizing experimental clusters (proof of concept, etc.), then it is probably best to delete the server and client stacks and either look for weaker instances, try a different AWS region, or try On-Demand instances instead of Spot. It would be futile trying to substitute the terminated instance because AWS is apparently running into Spot instance availability issues.
  <br><br>
</details>

---

<details>
  <summary>10. Why can I not add a materialized view (MV) in the running product?</summary>
  <br>
  We have not yet implemented that functionality. It requires setting up long-running transactions to transfer data between different nodes in the cluster. This functionality requires thorough stress testing and failure testing before release. 
  <br><br>
  At the moment, it is better to figure out your schema and test it before deployment. In a production system, this is not always doable as changes in schema are inevitable. We plan to make improvements in this area ASAP.
  <br><br>
</details>


---

<details>
  <summary>11. What if a write query timed out, should i resubmit it?</summary>
  <br>
  A write could timeout due to multiple reasons, either the write was never submitted to the leader node before it failed over to a different leader or write was submitted but the completion response did not arrive within the timeout window at the client. Depending upon the situation we have to perform different actions. The tests provided peachdb_client_cqldb.c etc contain detailed explanation of what to do in each case.
Simply resubmitting a write that may already have been submitted is not the correct approach. We have to determine the status of the previously submitted write and only proceed from there as explained in the tests.
  <br><br>
</details>

---

<details>
  <summary>12. What is the default timeout of queries?</summary>
  <br>
  We are setting a default timeout of 2 minutes, and this is the minimum timeout allowed because in the event of timeout we do not want to overwhelm the servers with continuous barrage of resubmissions of timed out requests.
  <br><br>
</details>

---

<details>
  <summary>13. I set up the server and client AWS Cloudformation Stacks and run the client but absolutely nothing happens, what is going on?</summary>
  <br>
  This situation is often caused because network traffic within the subnet has not been enabled as explained in <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Step1--Prerequisites.md"> prerequisites</a>
  <br><br>
</details>

---

<details>
  <summary>14. What is the topology of the server cluster?</summary>
  <br>
  In this database there are several co-ordinator nodes which receive all journals for the write transactions submitted to the cluster. The remaining members of the cluster receive writes only pertaining to their node. One node out of the co-ordinators is the leader that manages the raft consensus protocol. If the leader fails, a different co-ordinator node will be elected the leader. Writes get submitted to the leader and are propogated to the other members. In order for writes to be accepted more than K/2 co-rodinators have to be alive.
  <br><br>
</details>

---

<details>
  <summary>15. Are there any scheduled tasks to be run on the database servers?</summary>
  <br>
  Any such tasks are currently managed within the database server threads and do not require explicit tuning or scheduling. With the exception of vnode_gc for garbage collecting vnodes after cluster configuration changes and table_ops tasks for create/drop SI/Tables, the other tasks should not have any noticeable impact on performance. For the moment the user should not focus on this, we will check later if there is any need for any tunable parameters or not.
  <br><br>
</details>

---

<details>
  <summary>16. What are the tools to be aware of?</summary>
  <br>
  The following tools are utilized with peachydb:<br><br>
  <b>16.a</b><code>peachydb_cluster_tool</code>: to create/delete/describe server and client AWS Cloudformation Stacks. It is also utilized to modify (add,remove instances) client stacks. This tool is executed on the <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Step2--ControlUnit.md">Control Unit.</a><br><br>
  <b>16.b</b><code>peachy_modify_server_stack</code>: to modify server AWS Cloudformation stack. Modifying server stack is a database transaction and hence must be submitted from a client instance in the client AWS cloudformation stack.<br><br>
  <b>16.c</b><code>peachydb_status_tool</code>: to obtain support bundle and to monitor the health of server AWS Cloudformation stack. It is executed on the <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Step2--ControlUnit.md">Control Unit.</a>
  <br><br>
  <b>16.d</b><code>peachydb_repair_cluster</code>: to repair server AWS Cloudformation stack, if a previous server Stack modification operation left behind AWS EC2 instances that were meant to be deallocated. The repair is only of the AWS resources, it has no effect on the underlying database. <b>IMPORTANT</b>: It is the responsibility of the user to ascertain that any AWS EC2 instances being removed by this tool are not being utilized by the underlying cluster database. It is executed on the <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Step2--ControlUnit.md">Control Unit.</a>
  <br><br>
</details>

---
