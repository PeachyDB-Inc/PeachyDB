## Control Unit
This is the first step in deployment of Severs and Client Stacks. Obtain a control unit for managing the cluster of Server/Client stacks. The control unit can be cheapest EC2 instance (such as t2.nano or similar) running the product AMI. The goals of this instance are twofold:

1) Execute Server/Client Stack management commands

2) Utilize background scripts to monitor the Server Stack (Instance failure, utilization, core dumps, performance problems).
