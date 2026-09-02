1. peachydb_repair_cluster.py can be utilized to cleanup AWS state if a cluster configuration change did not go as planned. It provides two functionalities at the moment. Removing an AWS instance from the AWS server Cloudformation Stack. And the other functionality is to update the state of the stack template to indicate that the previous change to AWS Server Stack was completed and any left behind state in the AWS template should be reset.

```shell
  AWS Cluster Repair. For an AWS instance which is no longer part of
  a server cluster but is still running as a part of AWS stack, this
  tool can be utilized to remove such an AWS instance from the AWS stack
   Repair options:
   1 - PeachyDB Server stack remove instances
   2 - PeachyDB Server stack reset change state
   q  - Quit - exit this tool
  
  Enter Choice [1-2, q]:
```

2. Removing an AWS instance from an AWS Cloudformation Server Stack can be done by selecting choice 1 above. It is **IMPORTANT** to ascertain that the AWS Instance being removed is no longer a part of the peachydb server cluster. This is confirmed by utilizing peachydb_status_tool -status as explained <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> here</a>. If the IP address of the AWS instance being removed is not listed only then the AWS instance can safely be removed from the server stack. <br><br>
```shell
  AWS Cluster Repair. For an AWS instance which is no longer part of
  a server cluster but is still running as a part of AWS stack, this
  tool can be utilized to remove such an AWS instance from the AWS stack
   Repair options:
   1 - PeachyDB Server stack remove instances
   2 - PeachyDB Server stack reset change state
   q  - Quit - exit this tool
  
  Enter Choice [1-2, q]: 1

Removing AWS instances to repair a server cluster must only be done
if the instances are no longer part of the cluster and were mistakenly
not removed from the AWS stack by the cluster tools automatically
To confirm instances are no longer part of peachydb cluster do the following:
1. utilize status option of peachydb_status_tool.py to get peachydb server cluster members
2. utilize describe cluster option of peachydb_cluster_tool.py to obtain AWS stack instances
3. validate that the instance in 1. and 2. are not the same (compare ipaddrs)

Do you want to proceed (y/n)? y
Really? Please type the word \"proceed\" to continue: proceed
 Delete AWS Instances Arguments: <stack-name> <aws-instance-id>+
 AWS Instance Ids can be obtained by describing the cluster with peachydb_cluster_tool

Enter Arguments: myteststack i-0b5089d37bd750dd3
```
