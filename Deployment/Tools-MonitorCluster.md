## Monitor Server Cluster

It is recommended that you monitor the health of the server cluster utilizing <code>peachydb_status_tool</code>

1\. As explained in <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Step2--ControlUnit.md">control unit</a>, the control instance is the instance from which AWS cloud formation stacks are created and deleted. This is the instance from which the monitoring commands will be issued. From the control instance utilize <code>peachydb_status_tool</code> for various usages. NOTE:: the status tool can be run from the control unit as well as from the client instances. It is more cost effective to utilize the cheapest EC2 instance available to utilize as the Control Unit. However if you are ssh'd into a client machine, these commands should work there as well.

2\. Note that the commands below provide information about the nodes in the server cluster listed by ipaddress assigned to the instances. If you want to know which AWS instance the address corresponds to you can describe the stack as exaplined <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tool3-DescribeServerStack.md">here</a>. Since the server stack description doesn't change unless you modify the server cluster you can save it in a file to revisit as needed.

3\. To obtain the support bundle utilize the following command:
   ```shell
   prompt> python3 peachydb_status_tool -support-bundle server-stack-name aws-instance-id
   ```
  The support bundle will also attempt to include any core files generated on the instance, this will significantly degrade the performance of the instance. A support_bundle will not be generated any more than once per ten minute window for a given server node.

4\. To check liveness of nodes in the server cluster this is the command, it is recommended that liveness be sampled every 2 minutes.
   ```shell
   prompt> python3 peachydb_status_tool.py -status server-stack-name
   
   No Cluster CFG change ongoing, Leaderid:1, liveness check (heartbeat recvd):
   Nodeid:0 IP:132.21.2.128 LIVE
   Nodeid:1 IP:132.21.3.10 LIVE
   Nodeid:2 IP:132.21.4.215 LIVE
   ```
   No Cluster CFG change ongoing means there is no outstanding Cluster Configuration Change (add/remove/substitute/rejoin node) in server cluster.
   leaderid is the nodeid of the current leader. The ipaddress of each node is listed so that it can be corelated with the output of <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tool3-DescribeServerStack.md"> stack description</a>.

5\. To check space utilization on the members of the server cluster utilize the following command, it is recommended that space utilization
   be checked every few hours.
   ```shell
   prompt> python3 peachydb_status_tool.py -utilization server-stack-name

   Nodeid:0 IP:152.21.2.26 DB-SPACE: 5GB LOG-SPACE: 4.5GB CAPACITY: 4.9TB UTILIZATION: 0%
   Nodeid:1 IP:152.21.3.31 DB-SPACE: 5GB LOG-SPACE: 4.5GB CAPACITY: 4.9TB UTILIZATION: 0%
   Nodeid:2 IP:152.21.4.152 DB-SPACE: 5GB LOG-SPACE: 4.5GB CAPACITY: 4.9TB UTILIZATION: 0%
   ```
   The command output displays for each node the amount of SSD storage space taken up by the database, the logs and the total capacity of the instance as well as % utilization of storage space. The space is displayed in MB (megabytes) or GB (gigabytes) or TB (terabytes).

Utilizing the above commands too frequently will place a burden of their own on the servers as a result, they should be utilized according to the above suggestions. The above commands can be invoked from a bash script and their output can be parsed utilizing shell utilities to look for liveness issues of nodes or for too high storage utilization on the members of the cluster.
