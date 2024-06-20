# 9.  Syntax <command#>
In the main menu, choice '9' is meant do display the syntax of all the commands 1 through 9 and meaning of parameters to the commands.

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

Enter Choice [1-9, q]: 9


Enter command# for Syntax (1-8 or q to quit): 1

Create Stack Arguments: aws-key-pair-name=<key-pair-name> s3-bucket-name=<bucket-name> stack-name=<name> security-group-id=<sgroup-id> subnet-id=<subnetid> aws-instance-type=<ec2-instance-type> number-of-instances=<num> number-of-coordinators=<num> ssh-location=<sshloc> spot-instance=[1/0] use-placement-groups=[1/0]
 Arguments Description:
 aws-key-pair-name=<keypair-name> - name of the AWS key-pair to utilize for AWS commands
 s3-bucket-name=<bucketname> - AWS S3 bucket name to be utilized for templates
 stack-name=<name> - AWS Stack name to be utilized for the PeachyDB cluster
 security-group-id=<sgroupid> - Security Group Id to utilize for the Cluster
 subnet-id=<subnetid> - Subnet ID to utilize for the cluster
 aws-instance-type=<type> - one of the supported instance types:
i3.2xlarge, i3.4xlarge, i3.8xlarge, i3.16xlarge, i3.metal, i3en.2xlarge, i3en.3xlarge, i3en.6xlarge, i3en.12xlarge, i3en.24xlarge, i3en.metal
 number-of-instances=<num> - between 3 to 64 instances (default 3)
 number-of-Coordinators=<num> - either 3, 5 or 7 co-ordinators (default 3)
 ssh-location=<sshloc> - The IP address range that can be used to SSH to the EC2 instances
                  Must be a valid IP CIDR range of the form x.x.x.x/x.
 spot-instance=[1/0] - 1/0 indicating whether or not to utilize Spot Instances
 use-placement-groups=[1/0] - 1/0 indicating whether or not to utilize Partition Placement Groups

Enter command# for Syntax (1-8 or q to quit): 2

Delete Stack Arguments: <stack-name>
 Arguments Description:
 <stack-name> - AWS Stack name to be utilized for the PeachyDB cluster

Enter command# for Syntax (1-8 or q to quit): 3

Describe Arguments: <stack-name> [<optional-output-file-name>]
 Arguments Description:
 <stack-name> - AWS Stack name of the PeachyDB cluster, whose description is needed
 [<optional-output-file-name>] - optionally specify file for output

Enter command# for Syntax (1-8 or q to quit): 4

Client Stack Create Arguments: aws-key-pair-name=<key-pair-name> s3-bucket-name=<bucket-name> peachydb-stack-name=<name> stack-name=<name> security-group-id=<sgroup-id> subnet-id=<subnetid> aws-instance-type=<ec2-instance-type> number-of-instances=<num> ssh-location=<sshloc> spot-instance=[1/0]
 Arguments Description:
 aws-key-pair-name=<keypair-name> - name of the AWS key-pair to utilize for AWS commands
 s3-bucket-name=<bucketname> - AWS S3 bucket name to be utilized for templates
 peachydb-stack-name=<name> - AWS Stack name of the PeachyDB Server cluster
 client-stack-name=<name> - AWS Stack name to be utilized for the client cluster
 security-group-id=<sgroupid> - Security Group Id to utilize for the Cluster
 subnet-id=<subnetid> - Subnet ID to utilize for the cluster
 aws-instance-type=<type> - AWS instance type to be added to client cluster, valid types are:
m5.large, m5n.large, m5a.large, m5ad.large, c5.large, c5n.large, c5a.large, c5ad.large, i3.large, m5.xlarge, m5n.xlarge, m5a.xlarge, m5ad.xlarge, c5.xlarge, c5n.xlarge, c5a.xlarge, c5ad.xlarge, i3.xlarge, m5.2xlarge, m5n.2xlarge, m5a.2xlarge, m5ad.2xlarge, c5.2xlarge, c5n.2xlarge, c5a.2xlarge, c5ad.2xlarge, m5.4xlarge, m5n.4xlarge, m5a.4xlarge, m5ad.4xlarge, c5.4xlarge, c5n.4xlarge, c5a.4xlarge, c5ad.4xlarge, m5.8xlarge, m5n.8xlarge, m5a.8xlarge, m5ad.8xlarge, c5.9xlarge, c5n.9xlarge, c5a.8xlarge, c5ad.8xlarge
 number-of-instances=<num> - between 3 to 64 instances (default 3)
 ssh-location=<sshloc> - The IP address range that can be used to SSH to the EC2 instances
                  Must be a valid IP CIDR range of the form x.x.x.x/x.
 spot-instance=[1/0] - 1/0 indicating whether or not to utilize Spot Instances

Enter command# for Syntax (1-8 or q to quit): 5

Client Stack delete Arguments: client-stack-name=<name>
 Arguments Description:
 client-stack-name=<name> - AWS Client Stack name to delete

Enter command# for Syntax (1-8 or q to quit): 6

Client Stack Add Instance Arguments: <client-stack-name> <num-instances> <aws-instance-type>
 Arguments Description:
 <client-stack-name> - AWS Stack name for client machines
 <num-instances> - number of AWS instances to be added for client machines
 <aws-instance-type> - AWS instance type to be added to client cluster, valid types are:
m5.large, m5n.large, m5a.large, m5ad.large, c5.large, c5n.large, c5a.large, c5ad.large, i3.large, m5.xlarge, m5n.xlarge, m5a.xlarge, m5ad.xlarge, c5.xlarge, c5n.xlarge, c5a.xlarge, c5ad.xlarge, i3.xlarge, m5.2xlarge, m5n.2xlarge, m5a.2xlarge, m5ad.2xlarge, c5.2xlarge, c5n.2xlarge, c5a.2xlarge, c5ad.2xlarge, m5.4xlarge, m5n.4xlarge, m5a.4xlarge, m5ad.4xlarge, c5.4xlarge, c5n.4xlarge, c5a.4xlarge, c5ad.4xlarge, m5.8xlarge, m5n.8xlarge, m5a.8xlarge, m5ad.8xlarge, c5.9xlarge, c5n.9xlarge, c5a.8xlarge, c5ad.8xlarge

Enter command# for Syntax (1-8 or q to quit): 7

Client Stack Remove Instances Arguments: <client-stack-name> <aws-instance-id>+
 Arguments Description:
 <client-stack-name> - AWS Stack name for client machines
 <aws-instance-id>+ - one or more instance ids of the AWS instances to remove from client cluster

Enter command# for Syntax (1-8 or q to quit): 8

Client Stack Describe Arguments: <client-stack-name> [<optional-output-file-name>]
 Arguments Description:
 <client-stack-name> - AWS Stack name for client machines
 [<optional-output-file-name>] - optionally specify file for output

Enter command# for Syntax (1-8 or q to quit): q


 PeachyDB server utilities (1-3):
 1 - Create - create a Stack to run a PeachyDB cluster
 2 - Delete Stack - remove the specified PeachyDB cluster stack
 3 - Describe - Describe the nodes in the PeachyDB cluster

 Client utilities (4-8):
 4 - Client Stack Create - create a client Stack with a given name
 5 - Client Stack Delete - delete a client Stack with a given name
 6 - Client Stack Add Instances - add one or more EC2 instance to the client Stack
 7 - Client Stack Remove Instances - remove one or more EC2 instance from a client Stack
 8 - Client Stack Describe - describe all client instances in the client Stack
 9 - Syntax <command#> - syntax for any commands (1 to 8) listed above
 q  - Quit - exit this tool

Enter Choice [1-9, q]: q
```

The above are the various command line arguments to the different commands available in peachydb_cluster_tool.py
