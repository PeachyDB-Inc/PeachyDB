#### Frequently Asked Questions

<details>
  <summary>1. Write performance tanked, what happened?</summary>
  <br>
  Writes in this database are subject to the following constraints:
  <ul>
    <li>1.a) Each write will respond to the client only after all live servers which involve that write have responded to the leader node. Thus, a server node will slow down the writes that involve that server.</li>
    <li>1.b) If a server has restarted/failed, then writes will be throttled significantly (as much as 70%) to allow the node to rejoin the cluster. Otherwise, so much write log will be accumulated that the server may not be able to catch up after restart. Thus, any failed node in the cluster should be brought back online as quickly as possible.</li>
    <li>1.c) Cluster Configuration change (node add/delete/substitute) is ongoing. During this change, we have to deal with long-running read transactions which accumulate MVCC versions; to mitigate this, we throttle the writes significantly.</li>
    <li>1.d) Write performance is also impacted a little by utilizing Partition Placement groups. However, for product clusters, using Placement groups is more or less required.</li>
    <li>1.e) A small performance loss may also be incurred (if the database is small enough to fit in cache) due to requirements of the Raft consensus protocol.</li>
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
    <li>Occasionally, it has been found that the ports could not be bound to DPDK and you will see the <code>peachdb.messglog</code> indicating so. There is an open GitHub issue for this case.</li>
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
  If the failure is at AWS and the instance cannot be revived, you have to substitute the instance. In the presence of multiple correlated failures, if all servers responsible for a vnode have failed, you will experience data loss.
  <br><br>
</details>

---

<details>
  <summary>4. How do I obtain a support bundle?</summary>
  <br>
  The <code>peachydb_status_tool</code> can be utilized for obtaining a support bundle. The <code>peachydb_status_tool</code> can be utilized to obtain support bundle for servers in the server AWS CloudFormation Stack. This is explained in <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> monitor servers </a>.
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
  <summary>6. I get "mvccro garbage collected" or "stale handle", what should I do?</summary>
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
  <br>
  If you are utilizing experimental clusters (proof of concept, etc.), then it is probably best to delete the server and client stacks and either look for weaker instances, try a different AWS region, or try On-Demand instances instead of Spot. It would be futile trying to substitute the terminated instance because AWS is apparently running into availability issues.
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
