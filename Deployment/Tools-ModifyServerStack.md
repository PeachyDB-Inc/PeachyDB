## Modify Server Stack
Adding/Removing/Substituting Instances in a Server Stack is considered to be a Database Transaction and thus it is not accomplished via peachydb_cluster_tool.py. Instead we have to rely on a different tool for this, which can be executed only from a client machine. This tool is called peachydb_modify_server_stack.py.

***__IMPORTANT:__*** Cluster changes require long running read-only transactions that scan the whole database. Thus we have to keep some issues in mind:
1. Long running read only transactions, running in presence of writers, have a tendency to accumulate MVCC versions of data.
2. In order to mitigate this to some degree we significantly throttle the write transactions during cluster changes. However this may not be enough. If writes continue they will a) slow down the read-only transactions and b) they will keep accumulating older data and eventually even double the size of the database.
3. Thus if the database storage space is already at the brink initiating a cluster change during write activity will run into out of storage space issue.
4. It is best to perform such changes at quiescent points (eg. 12:00 AM Sunday night), with minimal write activty. Even with a large database that requires transfer of 100s of GBs of data to the new node, the change should complete relatively quickly. Larger AWS EC2 instances can handle even more data. However if you invoke the operation during heavy write activity, the same operation may take much longer and there is a real danger that it will run into out of disk space due to older versions.
5. We may have some improvements in this area, but they can not eliminate the issue of older MVCC versions.

As a prerequisite please carefully read setions in <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#14-adding-nodes-to-the-cluster" title="Documentation"> Section 14</a>, <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#15-replacing-a-failed-node" title="Documentation"> Section 15</a>, <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#16-removing-nodes-from-the-cluster" title="Documentation"> Section 16 </a> and <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#24-known-bugs-issues" title="Documentation"> Known Bugs, Issues </a> of Readme.md to understand numerous issues.

**Steps:**

1. As a first step obtain an EC2 Instance in the Client Stack from which the Server Stack Modification will be issued. If such an instance is not available then follow the steps  <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool6-ClientStackAddInstances.md" title="Documentation"> Adding Instances to Client Stacks</a> to add such a client instance. The EC2 Instance chosen can be one of the cheapest ones available.

2. Once the Client Instance is available we can now Add/Remove/Substitute instances to the Server Stack utilizing peachydb_modify_server_stack.py

```shell
  prompt> python3 peachydb_modify_server_stack.py
  1 - Stack Add Instance - add an EC2 instance to a server Stack
  2 - Stack Remove Instance - remove an EC2 instance from a server Stack
  3 - Stack Substitute Instance - substitute an EC2 instance in a server Stack
  4 - Stack Rejoin Instance - rejoin EC2 instance in a server Stack (factory reset of database followed by join)
  5 - Stack Cancel Ongoing change - cancel ongoing change
  q  - Quit - exit this tool

  Enter Choice [1-5, q]:
