### Monitor Server Cluster
It is recommended that you monitor the health of the server cluster utilizing <code>peachydb_status_tool</code>

1. As explained in <a href="">monitor instance</a>, the monitoring instance is the instance from which AWS cloud formation stacks are created and deleted. This is the instance from which the monitoring commands will be issued.
2. From the monitoring instance utilize <code>peachydb_status_tool</code> for various usages.
3. To obtain the support bundle utilize the following command:
  <code></code>
4. To check liveness of nodes in the server cluster this is the command, it is recommended that liveness be sampled every 2 minutes.
   <code></code>
5. To check space utilization on the members of the server cluster utilize the following command, it is recommended that space utilization
   be checked every few hours.
   <code></code>
