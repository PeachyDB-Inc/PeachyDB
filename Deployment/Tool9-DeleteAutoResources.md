## 9. Delete auto generated AWS resources
If you created server/client AWS stacks with auto generated S3 bucket and auto generated Security Group and Subnet,
then the command below can be utilized to delete these resources. Running this command is only needed if you are
no longer utilizing PeachyDB or if you plan to utilize your own S3 bucket and Security Group and Subnet for 
creating server/client AWS stacks. These resources are created once per region and for every subsequent creation
of stacks the resources are reutilized.

```
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

Common:
 9 - Delete Auto Generated AWS resources: S3 bucket, Security Group and Subnet
 10 - Syntax <command#> - syntax for any commands (1 to 9) listed above
 q  - Quit - exit this tool

Enter Choice [1-9, q]: 9

Delete auto generated resources (s3 bucket, security group, subnet) Arguments: vpc-id=<aws-vpc-id-name>
Enter Arguments: vpc-id=vpc-2127c314

Removing Auto created AWS S3 bucket ...
Emptying all objects from 'peachydb-352163213483-us-west-1' before deletion...
Deleting bucket: peachydb-352163213483-us-west-1 ...
Successfully deleted bucket: peachydb-352163213483-us-west-1

Removing Auto created Subnet ...
Searching for subnets with tag AWS-Marketplace-PeachyDB-Product=PeachyDB-Subnet ...
Attempting to delete subnet: subnet-0d6a45dfe43b23a8a
Successfully deleted subnet: subnet-0d6a45dfe43b23a8a

Removing Auto created Security Group ...
Searching for security groups with tag AWS-Marketplace-PeachyDB-Product=PeachyDB-SecurityGroup ...
Attempting to delete security group: peachydb-sg (sg-0f23ae1b422456729)
Successfully deleted security group sg-0f23ae1b422456729
```
