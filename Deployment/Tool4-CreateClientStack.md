## 4. Create Client Stack
In the main menu, choice '4' is meant for creating a Client Stack. Below is the command sequence to accomplish this.The arguments provided to create the Client Stack are a little too verbose so its best to write them in a file and then copy paste the arguments onto the prompt as indicated below. <br>

***NOTE::*** Client stack should be created only after the Server Stack has been successfully created because Clients need server configuration files which are only generated after the Server Stack has been successfully created. Otherwise Clients will not be able to communicate with the Servers. Similarly if a Server Stack is deleted, the Client Stack must also be deleted as its configuration files are no longer valid.

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

Enter Choice [1-9, q]: 4

Client Create Stack Arguments: aws-key-pair-name=<key-pair-name> s3-bucket-name=<bucket-name> peachydb-stack-name=<stack-name> client-stack-name=<name> security-group-id=<sgroup-id> subnet-id=<subnetid> aws-instance-type=<instance-type> number-of-instances=<num> ssh-location=<ssh-location> spot-instance=[1/0]

Enter Arguments: aws-key-pair-name=user-choice-key-pair-uswest1 s3-bucket-name=peachydb-test-bucket peachydb-stack-name=peachydb-teststack client-stack-name=peachydb-test-client-stack security-group-id=sg-2cc5c28c subnet-id=subnet-34233d12 aws-instance-type=m5.xlarge number-of-instances=1 ssh-location=03.102.20.36/32 spot-instance=1

```
As indicated the above Client stack is created with a single m5.xlarge instance with an EC2
Spot Instance. The server stack refered to by the client stack is indicated.
Please wait for the command to finish execution, do not Ctrl-C it (hitting Ctrl-C will not cancel the command, it has already been submitted to AWS), it might take a few minutes to create the stack, keep an eye on the CloudFormation Console, if it is taking too long most likely its because Spot Instances are not available, the above command can be tried without Spot Instances. Allthough for Clients typically Spot Instances are available.
