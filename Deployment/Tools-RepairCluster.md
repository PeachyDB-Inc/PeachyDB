1. peachydb_repair_cluster.py can be utilized to cleanup AWS state if a cluster configuration change did not complete as expected. There are two issues it resolves with an impaired AWS server stack. <br><br>
 1.a) If peachydb_cluster_tool <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tool3-DescribeServerStack.md">describe</a> option lists an AWS EC2 instance, with an IP address, but that IP Address does not appear in peachydb_status_tool -status <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> output</a>, then you can remove the extra AWS EC2 instances by utilizing the remove command below.<br><br>
 1.b) If peachydb_cluster_tool describe option matches with the peachydb_status_tool -status by IP address and you still can not submit additional cluster configuration changes, then you can invoke option 2 of the repair tool to reset state of the AWS server Cloudformation template.<br><br>
This is what the repair tool output looks like.
```shell
  prompt> python3 peachydb_repair_cluster.py
  AWS Cluster Repair. For an AWS instance which is no longer part of
  a server cluster but is still running as a part of AWS stack, this
  tool can be utilized to remove such an AWS instance from the AWS stack
   Repair options:
   1 - PeachyDB Server stack remove instances
   2 - PeachyDB Server stack reset change state
   q  - Quit - exit this tool
  
  Enter Choice [1-2, q]:
```

2. Removing an AWS instance from an AWS Cloudformation Server Stack can be done by selecting choice 1 above. It is **IMPORTANT** to ascertain that the AWS Instance being removed is no longer a part of the peachydb server cluster. This is confirmed by utilizing peachydb_status_tool -status as explained <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> here</a>. If the IP address of the AWS instance being removed is not listed in output of status tool only then the AWS instance can safely be removed from the server stack. Be very careful when submitting this command do **NOT** submit InstanceIdof an AWS instance that is still in use, you will terminate a server instance which is alive and receiving data. You will have to go through the time consuming task of substituting the terminated AWS instance. And if multiple AWS instances were removed, you will effectively kill your database. So it is better to submit removal of one instance at a time.<br>
```shell
  prompt> python3 peachydb_repair_cluster.py
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

3. If you find that the cluster configuration change that you submitted completed, and you can confirm with peachydb_status_tool -status that all expected instances are live and you can also confirm from describing the stack with peachydb_cluster_tool that it does not contain any extraneous instances that the peachydb_status_tool is not aware of and you still can not submit additional cluster configuration changes. This means that the AWS Cloudformation template has a stale state which is preventing additional cluster configuration changes from being submitted and which needs to be reset by selecting option 2 of he repair tool as below.

 ```shell
  prompt> python3 peachydb_repair_cluster.py
  AWS Cluster Repair. For an AWS instance which is no longer part of
  a server cluster but is still running as a part of AWS stack, this
  tool can be utilized to remove such an AWS instance from the AWS stack
   Repair options:
   1 - PeachyDB Server stack remove instances
   2 - PeachyDB Server stack reset change state
   q  - Quit - exit this tool
  
  Enter Choice [1-2, q]: 2

AWS Server stack state can be reset only if ALL below conditions are true:")
1. You are unable to make cluster changes because the stack mistakenly")
  believes that a cluster configuration change is ongoing when none is active")
2. You have checked with status option of peachydb_status_tool.py as well as")
   describe cluster option of peachydb_cluster_tool.py that all instances in")
   AWS stack are same as the instances in the peachydb cluster")
3. There is no cluster configuration change that is currently ongoing")

If cluster members reported by peachydb_status_tool.py are fewer than the")
ones reported by peachydb_cluster_tool.py, please utilize repair to remove")
the AWS instances instead")
Alternately if there is a cluster configuration change ongoing you can cancel it")
utilizing the peachydb_modify_server_stack.py tool")

Do you want to proceed (y/n)? y
Please enter Stack Name: myteststack

```
  
