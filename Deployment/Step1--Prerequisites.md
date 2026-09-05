## Pre-requisites
Before you can start utilizing the product, the following requirements have to be met:
- Create an AWS EC2 Key Pair, which will be utilized to create Server and Client AWS Cloudformation stacks

**NOTE**: The steps below are **Optional**, if the user wants to create their own S3 bucket name, security-group and VPC and subnet they can follow these steps else they can allow the tools to auto-create these resources with auto generated names and or tags where applicable.  In order to auto-create security-group and subnet the user will have to provide a VPC-ID as that is not auto-generated. If these resources are auto-created they will be created only once for each specified AWS region and thereafter any subsequent invocations of the tools will lookup prior resource by names or tags in the AWS region and utilize those. So the very first attempt to create a stack will take extra time to create the below resources but thereafter the resources will be reutilized for any subsequent stack creation in the AWS region.

- Create an S3 bucket with write permissions for cluster config files, in keeping with AWS requirements the bucket must have a name with the prefix 'peachydb'. This bucket will contain configuration files of the AWS Cloudformation Stacks, which are needed by AWS Server as well as Client EC2 Instances.
- Create a Security Group to utilize for instances
- Create a VPC and Subnet to utilize for instances
- In the AWS Console, enable all traffic **WITHIN** the Subnet, VPC
  (Set up a traffic rule from within the security group for all ICMP IPV4 traffic within the group). Refer to point 23.k in <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#23-deployment-strategy" title="peachydb readme"> deployment strategy</a>.
- ALL servers and clients of a cluster must utilize the same Subnet and VPC

- Create an IAM role profile stack. This is created only once and thereafter all the stacks refer to this. This is done so that the user does not have to repeatedly spend minutes creating the role profile with every single stack creation. This introduces an additional pre-requisite but the subsequent savings of time for all the future stack creation makes it worthwhile. This step is **NOT** needed anymore as the python scripts provided automatically perform this step. Its being listed here so that the user is aware that an IAM role profile stack will also be created by the product the very first time the stack create command is invoked, thereafter the previously created IAM role profile will be utilized.
