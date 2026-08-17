## Modify Server Stack
Adding/Removing/Substituting Instances in a Server Stack is considered to be a Database Transaction and thus it is not accomplished via peachydb_cluster_tool.py. Instead we have to rely on a different tool for this, which can be executed only from a client machine. This tool is called peachydb_modify_server_stack.py.

***__IMPORTANT:__*** Cluster changes require long running read-only transaction. Thus we have to keep some issues in mind:
1. Long running transactions, running in presence of writers, have a tendency to accumulate MVCC versions of data.
2. In order to mitigate this to some degree we throttle the write transactions during cluster changes. However this may not be enough. If writes continue they will a) slow down the read-only transactions and b) they will keep accumulating older data and eventually even 2X the original database may be just older versions of data.
3. Thus if the database space is already at the brink initiating a cluster change during write activity will run into out of storage space issue.
4. It is best to perform such changes at quiescent points such as 12:00 AM Sunday night, with minimal write activty. Even with a large database that requires transfer of 100s of GB of data to the new node, the change should complete on the order of 20 minutes. However if you invoke the operation during heavy write activity, the same operation may take longer and there is a real danger that it will run into out of disk space due to older versions. 

**Steps:**

As a first step obtain an EC2 Instance in the Client Stack from which the Server Stack Modification will be issued. If such an instance is not available then follow the steps  <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool6-ClientStackAddInstances.md" title="Documentation"> Adding Instances to Client Stacks</a> to add such a client instance. The EC2 Instance chosen can be one of the cheapest ones available.

Once the Client Instance is available we can now Add/Remove/Substitute instances to the Server Stack. As a prerequisite please carefully read setions in <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#14-adding-nodes-to-the-cluster" title="Documentation"> Section 14</a>, <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#15-replacing-a-failed-node" title="Documentation"> Section 15</a>, <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#16-removing-nodes-from-the-cluster" title="Documentation"> Section 16 </a> and <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#24-known-bugs-issues" title="Documentation"> Known Bugs, Issues </a> of Readme.md to understand numerous issues.
