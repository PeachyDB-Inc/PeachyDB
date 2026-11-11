## Peachydb AWS Server Stack Repair

If Cluster Configuration Changes (add/remove/delete of nodes) in the server stack leave it in a state where the AWS Stack resources are not reconciled with the database, repair tool can be utilized to address issues.

**1\.** peachydb_repair_cluster.py can be utilized to cleanup AWS state if a cluster configuration change did not complete as expected. This tool only impacts AWS resources it has no impact on the underlying database. Since the tool does not interact with the product, it can be executed from the <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Step2--ControlUnit.md">Control Unit</a>. It can also be executed on a client machine. There are two issues it resolves with an impaired AWS server stack.<br><br>
 **1.a)** If peachydb_cluster_tool <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tool3-DescribeServerStack.md">describe</a> option lists an AWS EC2 instance, with an IP address, but that IP Address does not appear in peachydb_status_tool -status <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> output</a>, then you can remove the extra AWS EC2 instances by utilizing the remove command below. This issue can arise when an AWS Instance is being removed from the server stack and the database operation completes but the AWS cloud part of removing the instance got left behind due to corner cases that were not handled. Removing any such instances which are not utilzed by the underlying database is essential otherwise they will keep incurring AWS charges. The repair tool automatically identifies such AWS instances that need to be removed.<br><br>
 **1.b)** If peachydb_cluster_tool describe option matches with the peachydb_status_tool -status by IP address and you still can not submit additional cluster configuration changes, then the repair tool can be utilized to reset state of the AWS server Cloudformation template.<br><br>
This is what the repair tool output looks like.
```shell
  prompt> python3 peachydb_repair_cluster.py
  AWS Cluster Repair
  This tool is utilized for two purposes:

  1. Remove AWS EC2 instances which are still in AWS server stack even though
   the instances have been removed from the PeachyDB server cluster.
  
  2. Reset state of AWS Cloudformation Server Stack to clear any state
   that is not allowing submission of Cluster configuration changes (add/remove node etc)
   even though PeachyDB server cluster has completed prior cluster change operation.
```

**Prerequisite**: Before the repair actions are executed the repair tool automatically ascertains that there is no ongoing Cluster Configuration Change (add/remove/substitute) at the database level. This can also be explicitly done with the cluster <a href="https://github.com/PeachyDB-Inc/PeachyDB/blob/main/Deployment/Tools-MonitorCluster.md"> status tool.</a> The status tool should clearly state that no CFG (cluster configuraton change) is ongoing. If a CFG is ongoing at the database level then we have to firstly allow that to either complete or cancel it, before we can start repairing the AWS server stack. The repair tool makes the check and if there is a CFG in progress the repair tool will terminate.<br><br>

**2\.** The repair tool automatically identifies AWS EC2 instances which are still a part of the AWS stack but they have been removed from the PeachyDB server cluster. It will provide the option to remove these instances from the AWS stack to the user as shown here. Once the AWS EC2 instances between AWS Stack and PeachyDB server cluster have been reconciled, the tool will give the user the option to reset the stack state, which should always be performed with 'y' as indicated below. Note that if there are no EC2 instances to be removed then it will go to the step of resetting the state directly.<br>
```shell
prompt> python3 peachydb_repair_cluster.py
AWS Cluster Repair
This tool is utilized for two purposes:

1. Remove AWS EC2 instances which are still in AWS server stack even though
 the instances have been removed from the PeachyDB server cluster.

2. Reset state of AWS Cloudformation Server Stack to clear any state
 that is not allowing submission of Cluster configuration changes (add/remove node etc)
 even though PeachyDB server cluster has completed prior cluster change operation.

Please enter Stack Name: myteststack
Checking ongoing Cluster Configurgation change at PeachyDB level ...
No Cluster config change is ongoing in PeachyDB database
Checking for AWS Server Stack EC2 instances NOT part of PeachyDB server cluster ...

Found some AWS EC2 instances which may need to be removed ...
Removing AWS instances to repair a server cluster must only be done
if the instances are no longer part of the cluster and were mistakenly
not removed from the AWS stack by the cluster tools automatically

AWS Stack EC2 Instances not present in PeachyDB server cluster: ['i-0738498d5ae40416e', 'i-0728397c5df20314e']

Do you want to proceed with removal of AWS EC2 instances (y/n)? y
Really? Please type the word "proceed" to continue: proceed
Removing AWS instances from server stack. Please do NOT Ctrl-C, operation in progress ...
Waiting for AWS stack update to remove instance[s] to complete
Please check AWS console to ensure operation completes ...
. . . . . . . . . . . . . . . . . . .
Stack update complete
Done removing AWS EC2 instances from AWS Server Stack
AWS Server Stack Instances are reconciled with PeachyDB server cluster

Do you want to reset state of AWS Server Stack (y/n): y
Resetting state of AWS Cloudformation Server Stack

