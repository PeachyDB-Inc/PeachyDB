## Control Unit
This is the first step in deployment of Severs and Client Stacks. Obtain a control unit for managing the cluster of Server/Client stacks. The control unit can be cheapest EC2 instance (such as t2.nano or similar) running the product AMI. The goals of this instance are twofold:

1) Execute Server/Client Stack management commands

2) Utilize background scripts to monitor the Server Stack (Instance failure, utilization, core dumps, performance problems).

### Pre-requisite for running peachydb_cluster_tool
On the EC2 Monitor Instance you have to execute the following commands, before doing anything else. These commands must also
be executed on any client EC2 Instance that needs to run the cluster tool.

1. Set the AWS region variable as follows, utilize the region that you are utilizing, in this example, it is `us-west-2a`:

   ```shell
   prompt> export AWS_DEFAULT_REGION=us-west-2a
   
2. Adjust any clock skew on the Instance with the command indicated:

   ```shell
   prompt> sudo ntpdate pool.ntp.org

 (NOTE:: occasionally if you find that the peachydb_cluster_tool reports
  "stack with provided name does not exist", this can also happen due to clock
  skew which can be adjusted with this same command).
