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

2. Note: some of the commands below (remove, substitute, rejoin) require ip-address of an AWS EC2 instance being utilized in the server stack, this is obtained by utilizing peachydb_status_tool -status as explained <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> here</a>, which can be executed from the client as well as the monitor instance.

3. Once the Client Instance is available we can now Add/Remove/Substitute instances to the Server Stack utilizing peachydb_modify_server_stack.py. This is what the invocation looks like.
```shell
  prompt> python3 peachydb_modify_server_stack.py
  1 - Stack Add Instance - add an EC2 instance to a server Stack
  2 - Stack Remove Instance - remove an EC2 instance from a server Stack
  3 - Stack Substitute Instance - substitute an EC2 instance in a server Stack
  4 - Stack Rejoin Instance - rejoin EC2 instance in a server Stack (factory reset of database followed by join)
  5 - Stack Cancel Ongoing change - cancel ongoing change
  q  - Quit - exit this tool

  Enter Choice [1-5, q]:
```

4. To add an AWS EC2 instance to a server stack this is the command
```shell
  prompt> python3 peachydb_modify_server_stack.py
  1 - Stack Add Instance - add an EC2 instance to a server Stack
  2 - Stack Remove Instance - remove an EC2 instance from a server Stack
  3 - Stack Substitute Instance - substitute an EC2 instance in a server Stack
  4 - Stack Rejoin Instance - rejoin EC2 instance in a server Stack (factory reset of database followed by join)
  5 - Stack Cancel Ongoing change - cancel ongoing change
  q  - Quit - exit this tool
  Enter Choice [1-5, q]: 1

Add instance Arguments: <stack-name>
Enter Arguments: myteststack
```

5. To remove an AWS EC2 instance from a server stack this is the command
```shell
  prompt> python3 peachydb_modify_server_stack.py
  1 - Stack Add Instance - add an EC2 instance to a server Stack
  2 - Stack Remove Instance - remove an EC2 instance from a server Stack
  3 - Stack Substitute Instance - substitute an EC2 instance in a server Stack
  4 - Stack Rejoin Instance - rejoin EC2 instance in a server Stack (factory reset of database followed by join)
  5 - Stack Cancel Ongoing change - cancel ongoing change
  q  - Quit - exit this tool
  Enter Choice [1-5, q]: 2

WARNING:: Instance Substitute is not equivalent to Instance Remove followed by Instance Add
Removing nodes is done only for extremely rare situation of downscaling cluster
For Substituting a node utilize Subtitute command
Utilizing (Delete Instance + Add Instance) instead of Subtitute can imbalance the cluster
and can be an extremely slow operation. Please make certain that your use case is not Substitute
DO you want to proceed (y/n)? y
Delete instance Arguments: <stack-name> <instance-ip-addr>
Instance IP address can be obtained by describing the cluster with peachydb_status_tool -status
Enter Arguments: myteststack 192.32.34.10
```

6. To substitute an AWS EC2 instance in a server stack this is what has to be done
```shell
  prompt> python3 peachydb_modify_server_stack.py
  1 - Stack Add Instance - add an EC2 instance to a server Stack
  2 - Stack Remove Instance - remove an EC2 instance from a server Stack
  3 - Stack Substitute Instance - substitute an EC2 instance in a server Stack
  4 - Stack Rejoin Instance - rejoin EC2 instance in a server Stack (factory reset of database followed by join)
  5 - Stack Cancel Ongoing change - cancel ongoing change
  q  - Quit - exit this tool
  Enter Choice [1-5, q]: 3

Substitute instance Arguments: <stack-name> <instance-ip-addr>
Instance IP address can be obtained by describing the cluster with peachydb_status_tool -status
Enter Arguments: myteststack 192.23.45.10
```

7. To rejoin instance that exists in the AWS server stack this is the command
```shell
  prompt> python3 peachydb_modify_server_stack.py
  1 - Stack Add Instance - add an EC2 instance to a server Stack
  2 - Stack Remove Instance - remove an EC2 instance from a server Stack
  3 - Stack Substitute Instance - substitute an EC2 instance in a server Stack
  4 - Stack Rejoin Instance - rejoin EC2 instance in a server Stack (factory reset of database followed by join)
  5 - Stack Cancel Ongoing change - cancel ongoing change
  q  - Quit - exit this tool
  Enter Choice [1-5, q]: 4

NOTE::
1. Rejoin should only be performed on nodes which are offline and are
 too far behind in jnls that a regular catchup will not allow them to join
2. Rejoining a node will perform a factory reset on the node and reobtain all data
 and then catchup with the leader to come online
3. Have you tried to perform a product restart on node to check if it can catchup?
4. Rejoin can only be invoked when no other cluster change (add/remove/substitute/rejoin) is ongoing
5. While a node is rejoining, cluster changes (add/remove/subst/rejoin node) can not be submitted

Do you want to proceed with Rejoin? (y/n) y
Rejoin instance Arguments: <stack-name> <instance-ip-addr>
Instance IP address can be obtained by describing the cluster with peachydb_status_tool -status
Enter Arguments: myteststack 192.34.44.10
```

8. To cancel any of the add/remove/subtitute operations above this is the command it will not work for rejoin instance.
```shell
prompt> python3 peachydb_modify_server_stack.py
1 - Stack Add Instance - add an EC2 instance to a server Stack
2 - Stack Remove Instance - remove an EC2 instance from a server Stack
3 - Stack Substitute Instance - substitute an EC2 instance in a server Stack
4 - Stack Rejoin Instance - rejoin EC2 instance in a server Stack (factory reset of database followed by join)
5 - Stack Cancel Ongoing change - cancel ongoing change
q  - Quit - exit this tool
Enter Choice [1-5, q]: 5

Cancel cluster change Arguments: <stack-name>
Enter Arguments: myteststack
```
