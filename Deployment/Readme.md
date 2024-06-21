## Deployment
In this sub directory you will find the pre-requisites and tools to get going with PeachyDB Server and Client Stacks and start running a client driver in less than 10 minutes (Once the prerequisites have been completed). Below are handy links to the above documents. As explained in <a href="https://github.com/akseg73/PeachyDB/tree/main?tab=readme-ov-file#27-costs" title="costs"> costs</a>, it is possible to start off with a minimal cluster for less than $0.85/hour (based on costs in us-west-2 region and based upon availability of Spot Instances for Server and Client stacks).

1. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Step1--Prerequisites.md" title="prereq">Pre-requisites</a> - This document explains the prerequisites to be able to run the commands for the PeachyDB Stacks.

2. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Step2--ControlUnit.md" title="control-unit"> Monitor Instance</a> - This is an AWS EC2 Instance that can be started with the peachyDB AMI to serve as a command center to manager PeachyDB stacks and to gather statistics, storage utilization and performance monitoring.

3. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Step3--PeachydbClusterTool.md" title="peachydb cluster tool">Peachydb Cluster Tool</a> -- peachydb_cluster_tool.py is the main tool to manage PeachyDB Server/Client Stack

4. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool1-CreateServerStack.md" title="create server stack">Create Server stack</a> --  how to create PeachyDB Server Stack

5. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool2-DeleteServerStack.md" title="delete server stack">Delete Server stack</a> -- how to delete PeachyDB Server Stack

6. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool3-DescribeServerStack.md" title="describe server stack">Describe Server stack</a> -- how to describe PeachyDB Server Stack

7. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool4-CreateClientStack.md" title="create client stack">Create Client Stack</a> -- how to create PeachyDB Client Stack

8. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool5-DeleteClientStack.md" title="delete client stack">Delete Client Stack</a> -- how to delete PeachyDB Client Stack

9. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool6-ClientStackAddInstances.md" title="add client instances">Add one or more EC2 Instances to Client Stack</a> -- how to Add one or more instances to Client Stack

10. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool7-ClientStackRemoveInstances.md" title="remove client instances">Remove one or more EC2 Instances from Client Stack</a> -- how to Remove one of more instances from client Stack

11. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool8-DescribeClientStack.md" title="describe client stack">Describe Client Stack</a> -- how to describe PeachyDB Client Stack

12. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tool9-CommandSyntax.md" title="syntax commands">Syntax for any of the above commands</a> -- explain syntax and arguments for each of the peachydb_cluster_tool.py commands. Although the commands themselves are quite explanatory.

13. <a href="https://github.com/akseg73/PeachyDB/blob/main/Deployment/Tools-ModifyServerStack.md" title="modify server stack">Modify Server Stack</a> -- a different tool has to be utilized to add/remove/substitute AWS EC2 Instances in the PeachyDB Server Stack.
