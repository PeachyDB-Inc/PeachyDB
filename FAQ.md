# PeachDB Frequently Asked Questions (FAQ)

This document provides answers to common questions, troubleshooting steps, and operational guidelines for PeachDB clusters.

---

### 1. Write performance tanked, what happened?
<details>
<summary><b>View Answer</b></summary>

Writes in this database are subject to the following constraints:
* **Node Responsiveness:** Each write will respond to the client only after all live servers involved in that write have responded to the leader node. A single slow server node will slow down all writes involving it.
* **Throttling During Rejoin:** If a server has restarted or failed, writes will be throttled significantly (as much as 70%) to allow the node to rejoin the cluster. This prevents accumulating too much write log for the server to catch up after a restart. **Recommendation:** Bring failed nodes back online as quickly as possible.
* **Ongoing Configuration Changes:** During cluster configuration changes (adding, deleting, or substituting nodes), writes are heavily throttled to mitigate long-running read transactions that accumulate MVCC versions.
* **Partition Placement Groups:** Write performance is slightly impacted by utilizing Partition Placement groups. However, using placement groups is effectively required for production clusters.
* **Raft Consensus Protocol:** A small performance loss may be incurred due to Raft consensus requirements if the database is small enough to fit completely in the cache.
</details>

---

### 2. The new node being added to the cluster appears to be hung, what should I do?
<details>
<summary><b>View Troubleshooting Steps</b></summary>

Follow these steps to investigate:
1. **SSH into the AWS instance:**
   ```bash
   ssh <aws-instance>
   ```
2. **Navigate to the store directory:**
   ```bash
   cd /dbstore
   ```
3. **Check the message log:**
   ```bash
   tail -f mypeachdb/peachdb.messglog
   ```
4. **Check for errors:**
   ```bash
   tail -f /var/log/peachy.errors
   ```

*Note: Occasionally, ports fail to bind to DPDK, which will be indicated in the `peachdb.messglog`. There is an open GitHub issue for this specific case.*

**If there is a failure:**
1. Cancel the cluster configuration change.
2. Resubmit the change.
</details>

---

### 3. An AWS EC2 server stack instance died, what should I do?
<details>
<summary><b>View Answer</b></summary>

If the failure is on the AWS side and the instance cannot be revived, you must **substitute the instance**. 

> ⚠️ **Warning:** In the presence of multiple correlated failures, if all servers responsible for a `vnode` have failed, you will experience data loss.
</details>

---

### 4. How do I obtain a support bundle?
<details>
<summary><b>View Command</b></summary>

Utilize the `peachydb_status_tool` with the `-support-bundle` flag:

```bash
peachydb_status_tool -support-bundle <stack-name> <aws-instance-id>
```
</details>

---

### 5. How do I monitor the server cluster?
<details>
<summary><b>View Monitoring Guidelines</b></summary>

Use the `peachydb_status_tool` to obtain liveness information for servers in your AWS CloudFormation Stack and to determine SSD utilization across various nodes:

```bash
# Check cluster liveness
peachydb_status_tool -status <stack-name>

# Check SSD utilization
peachydb_status_tool -utilization <stack-name>
```

#### Monitoring Schedule
* **`-status`**: Check every **2 minutes**.
* **`-utilization`**: Check every **hour**.
* *Warning:* Do not invoke these commands too frequently, as doing so will place an unnecessary burden on the servers.

#### Troubleshooting a Hung Server
If you find a server is hung, take the following actions:
1. Collect the support bundle and examine these logs:
   * `peachdb.messglog*`
   * `perf.peachdb.messglog*`
   * `bulk.peachdb.messglog*`
2. Generally, it is best **not** to SSH into server instances. However, if a node is completely unresponsive and you suspect an issue, you can make a quick check by running:
   ```bash
   ssh <server>
   cd /dbstore
   tail -f mypeachdb/peachdb.messglog
   ```
</details>

---

### 6. I get "mvccro garbage collected" or "stale handle", what should I do?
<details>
<summary><b>View Answer</b></summary>

This error should hopefully not occur outside of long-running read transactions. Reads (whether short or long-running) that involve only a single partition key should never see this error. 

* **File a Bug:** If you believe you are seeing this issue on short reads, please file an issue in GitHub. This may be a bug where a race condition assigns an incorrect distributed `jnltxid` to a reader.
* **Resolution:** This error means the read request failed and must be resubmitted by the client.
</details>

---

### 7. How do I determine if clients or the servers are the bottleneck?
<details>
<summary><b>View Diagnostics Guide</b></summary>

1. **Check the Client:** Run `mpstat` on the client node. You will likely find an uneven distribution of load across different cores. This happens because core 0 is assigned to the OS for system tasks.
2. **Interpret CPU Utilization:** If client CPU utilization is low, the server is likely the bottleneck. 
3. **Check the Server:** Examine `mypeachdb/perf.peachdb.messglog` to check CPU utilization for reads and writes. The servers may be either CPU or I/O bound.
4. **Experiment:** Try increasing the number of client threads or spinning up an additional client node. If neither change improves performance, the servers are officially bottlenecked on either CPU or I/O.
</details>

### 8. Why does `mpstat` on the client show an uneven distribution of CPU utilization?
<details>
<summary><b>Click to expand answer</b></summary>

Some of this is due to variances of system calls, and some is due to CPU core 0 being utilized by the OS. Therefore, the numerous cores are not uniformly available to the client.
</details>

---

### 9. A Spot instance in AWS was terminated, what should I do?
<details>
<summary><b>Click to expand answer</b></summary>

Spot instances for server AWS CloudFormation stacks are not recommended for production because they can be asynchronously terminated by AWS due to demand from other users. 

If you are utilizing experimental clusters (proof of concept, etc.), it is probably best to delete the server and client stacks and try one of the following:
* Look for weaker instance types.
* Try a different AWS region.
* Use **On-Demand** instances instead of Spot instances.

It would be futile to try to substitute the terminated instance right away because AWS is likely running into capacity availability issues in that specific zone/region.
</details>

---

### 10. Why can I not add a materialized view (MV) in the running product?
<details>
<summary><b>Click to expand answer</b></summary>

We have not yet implemented that functionality. It requires setting up long-running transactions to transfer data between different nodes in the cluster. This functionality requires thorough stress testing and failure testing before release. 

At the moment, it is better to finalize your schema and test it before deployment. While changes in schema are inevitable in a production system, we plan to make improvements in this area as soon as possible.
</details>
# Frequently Asked Questions

<details>
<summary><b>8. Why does <code>mpstat</code> on the client show an uneven distribution of CPU utilization?</b></summary>
<br>

Some of this is due to variances of system calls, and some is due to CPU core 0 being utilized by the OS. Therefore, the numerous cores are not uniformly available to the client.
</details>

---

<details>
<summary><b>9. A Spot instance in AWS was terminated, what should I do?</b></summary>
<br>

Spot instances for server AWS CloudFormation stacks are not recommended for production because they can be asynchronously terminated by AWS due to demand from other users. 

If you are utilizing experimental clusters (proof of concept, etc.), it is probably best to delete the server and client stacks and try one of the following:
* Look for weaker instance types.
* Try a different AWS region.
* Use **On-Demand** instances instead of Spot instances.

It would be futile to try to substitute the terminated instance right away because AWS is likely running into capacity availability issues in that specific zone/region.
</details>

---

<details>
<summary><b>10. Why can I not add a materialized view (MV) in the running product?</b></summary>
<br>

We have not yet implemented that functionality. It requires setting up long-running transactions to transfer data between different nodes in the cluster. This functionality requires thorough stress testing and failure testing before release. 

At the moment, it is better to finalize your schema and test it before deployment. While changes in schema are inevitable in a production system, we plan to make improvements in this area as soon as possible.
</details>

