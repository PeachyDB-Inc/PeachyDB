## Control Unit
This is the first step in deployment of Severs and Client Stacks. Obtain a control unit for managing the cluster of Server/Client stacks. The control unit can be cheapest EC2 instance (such as t2.nano or similar) running the product AMI. The goals of this instance are twofold:

1) Execute Server/Client Stack management commands

2) Utilize background scripts to monitor the Server Stack (Instance failure, utilization, core dumps, performance problems).

### Pre-requisite for running peachydb_cluster_tool
On the Control Monitor EC2 Instance you have to execute the following commands, before doing anything else.

1. Set the AWS region variable as follows, utilize the region that you are utilizing, in this example, it is `us-west-2a`:

   ```shell
   prompt> export AWS_DEFAULT_REGION=us-west-2a
   
2. Adjust any clock skew on the Instance with the command indicated:

   ```shell
   prompt> sudo date -s "$(wget -qSO- --max-redirect=0 google.com 2>&1 | grep Date: | cut -d' ' -f5-8)Z"

 (NOTE:: occasionally if you find that the peachydb_cluster_tool reports
  "stack with provided name does not exist", this can also happen due to clock
  skew which can be adjusted with this same command).
