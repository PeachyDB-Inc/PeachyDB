# 1. Modify Server Stack
Adding/Removing/Substituting Instances in a Server Stack is considered to be a Database Transaction and thus it is not accomplished via peachydb_cluster_tool.py. Instead we have to rely on a different tool for this, which can be executed only from a client machine. This tool is called peachydb_modify_server_stack.py.

As a first step obtain an EC2 Instance in the Client Stack from which the Server Stack Modification will be issued. If such an instance is not available then follow the steps Adding Instances to Client Stacks.

Once the Client Instance is available we can now Add/Remove/Substitute instances to the Server Stack. As a prerequisite please carefully read setions in Readme to understand numerous issue.
