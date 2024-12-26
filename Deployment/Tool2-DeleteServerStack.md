## 2. Delete Server Stack
In the main menu, choice '2' is meant for deleting a Server Stack. Below is the command sequence to accomplish this.

```shell
prompt> python3 peachydb_cluster_tool.py

Please utilize ONLY peachydb_cluster_tool.py for creating/deleting/updating PeachyDB clusters. DO NOT
Add/Remove/Substitute nodes without peachydb_cluster_tool.py, the cluster will become unusable.

Prerequisites:
 - Create an AWS EC2 Key Pair
 - Create an S3 bucket with write permissions for cluster config files
 - Create a Security Group to utilize for instances
 - Create a VPC, Subnet to utilize for instances
 - In AWS Console enable all traffic WITHIN the Subnet, VPC
ALL servers and clients of a cluster must utilize the same Subnet and VPC
Have you met the pre-requisites? [y/n]: y

PeachyDB Server Utilities (1-3):
 1. Create - create a Stack to run a PeachyDB cluster
 2. Delete Stack - remove the specified PeachyDB cluster stack
 3. Describe - Describe the nodes in the PeachyDB cluster

Client Utilities (4-8):
 4 - Client Stack Create - create a client Stack with a given name
 5 - Client Stack Delete - delete a client Stack with a given name
 6 - Client Stack Add Instances - add one or more EC2 instance to the client Stack
 7 - Client Stack Remove Instances - remove one or more EC2 instance from a client Stack
 8 - Client Stack Describe - describe all client instances in the client Stack
 9 - Syntax <command#> - syntax for any commands (1 to 8) listed above
 q  - Quit - exit this tool

Enter Choice [1-9, q]:2


Delete Stack Arguments: <stack-name>

Enter Server Stack name: peachydb-teststack

 WARNING:: Deleting the stack will delete all of the data, the operation can not be undone
Do you want to proceed (y/n)? y
Really? Please type the word "delete" to continue: delete
```


Please wait for the command to finish execution, do not Ctrl-C it (hitting Ctrl-C will not cancel the command, it has already been submitted to AWS and it has to be allowed to complete). This operation can take some time to complete. Just to be double sure keep an eye on the Cloudformation Console to make sure that everything is deleted. 
