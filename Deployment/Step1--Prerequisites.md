## Pre-requisites
Before you can start utilizing the product, the following requirements have to be met:
- Create an AWS EC2 Key Pair
- Create an S3 bucket with write permissions for cluster config files
- Create a Security Group to utilize for instances
- Create a VPC and Subnet to utilize for instances
- In the AWS Console, enable all traffic **WITHIN** the Subnet, VPC
  (Set up a traffic rule from within the security group for all ICMP IPV4 traffic within the group). Refer to point 23.k in <a href="https://github.com/akseg73/PeachyDB?tab=readme-ov-file#23-deployment-strategy" title="peachydb readme"> deployment strategy</a>.
- ALL servers and clients of a cluster must utilize the same Subnet and VPC

- Create an IAM role profile stack. This is created only once and thereafter all the stacks refer to this. This is done so that the user does not have to repeatedly spend minutes creating the role profile with every single stack creation. This introduces an additional pre-requisite but the subsequent savings of time for all the future stack creation makes it worthwhile.

### Before running the utility, do the following:
1. Set the AWS region variable as follows, utilize the region that you are utilizing, in this example, it is `us-west-2a`:

   ```shell
   prompt> export AWS_DEFAULT_REGION=us-west-2a
   
2. Adjust any clock skew on the Instance with the command indicated:

   ```shell
   prompt> sudo date -s "$(wget -qSO- --max-redirect=0 google.com 2>&1 | grep Date: | cut -d' ' -f5-8)Z"

 (NOTE:: occasionally if you find that the peachydb_cluster_tool reports
  "stack with provided name does not exist", this can also happen due to clock
  skew which can be adjusted with this same command).
