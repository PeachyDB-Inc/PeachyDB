# 8. Describe Client Stack
In the main menu, choice '8' is meant for describe the AWS EC2 Instances in the Client Stack. Below steps can accomplish this.

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

Enter Choice [1-9, q]: 8

Describe Arguments: <stack-name> [<optional-output-file-name>]

Enter Arguments: test-client-stack
```
The above will describe the Client Stack instances, and the output can also
be optionally saved in a file.
