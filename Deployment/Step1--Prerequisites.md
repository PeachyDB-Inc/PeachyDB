## Pre-requisites
Before you can start utilizing the product, the following requirements have to be met:
- Create an AWS EC2 Key Pair
The steps below are optional, if the user wants to provide security-group and their own VPC and subnet they can follow these steps else you can allow the tools to auto-create these by providing the parameter "auto" instead of S3 bucket name, security groupid and subnet id and then the tools will auto create these resource with default names and or tags where applicable.
- Create an S3 bucket with write permissions for cluster config files, in keeping with AWS requirements the bucket must have a name with the prefix 'peachydb'. This bucket will contain configuration files of the AWS Cloudformation Stacks, which are needed by AWS Server as well as Client EC2 Instances.
- Create a Security Group to utilize for instances
- Create a VPC and Subnet to utilize for instances
- In the AWS Console, enable all traffic **WITHIN** the Subnet, VPC
  (Set up a traffic rule from within the security group for all ICMP IPV4 traffic within the group). Refer to point 23.k in <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#23-deployment-strategy" title="peachydb readme"> deployment strategy</a>.
- ALL servers and clients of a cluster must utilize the same Subnet and VPC

- Create an IAM role profile stack. This is created only once and thereafter all the stacks refer to this. This is done so that the user does not have to repeatedly spend minutes creating the role profile with every single stack creation. This introduces an additional pre-requisite but the subsequent savings of time for all the future stack creation makes it worthwhile.
