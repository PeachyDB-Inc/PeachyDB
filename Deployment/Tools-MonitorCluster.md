## Monitor Server Cluster
It is recommended that you monitor the health of the server cluster utilizing <code>peachydb_status_tool</code>

1. As explained in <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Step2--ControlUnit.md">control unit</a>, the control instance is the instance from which AWS cloud formation stacks are created and deleted. This is the instance from which the monitoring commands will be issued. From the control instance utilize <code>peachydb_status_tool</code> for various usages. NOTE:: the status tool can be run from the control unit as well as from the client instances. It is more cost effective to utilize the cheapest EC2 instance available to utilize as the Control Unit. However if you are ssh'd into a client machine, these commands should work there as well.<br><br>
2. Note that the commands below provide information about the nodes in the server cluster listed by ipaddress assigned to the instances. If you want to know which AWS instance the address corresponds to you can describe the stack as exaplined <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tool3-DescribeServerStack.md">here</a>. Since the server stack description doesn't change unless you modify the server cluster you can save it in a file to revisit as needed.<br><br>
3. To obtain the support bundle utilize the following command:
  <br> <code> peachydb_status_tool -support-bundle stack-name aws-instance-id </code><br><br>
  The support bundle will also attempt to include any core files generated on the instance, this will significantly degrade the
   performance of the instance. A support_bundle will not be generated any more than once per ten minute window for a given server node.
   <br><br>
4. To check liveness of nodes in the server cluster this is the command, it is recommended that liveness be sampled every 2 minutes.
   <code>peachydb_status_tool -status stack-name </code><br><br>
5. To check space utilization on the members of the server cluster utilize the following command, it is recommended that space utilization
   be checked every few hours.
   <br><code>peachydb_status_tool -utilization stack-name</code>

Utilizing the above commands too frequently will place a burden of their own on the servers as a result, they should be utilized according to the above suggestions. The above commands can be invoked from a bash script and their output can be parsed utilizing shell utilities to look for liveness issues of nodes or for too high storage utilization on the members of the cluster.
