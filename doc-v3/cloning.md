# Cloning

## Basic Principle

Smit lets you:

- clone a remote Smit repository to your local computer
- browse your local copy
- pull remote changes to your local copy
- make local changes: new issues, modify properties of issues,...
- push your local changes to the remote repository

But, obviously, conflicts will happen: Alice will change a property on her laptop, and Bob will change the same property on his workstation. And when they will push the change to the same centralized repository, the second person to push will have a conflit.



## How Conflicts are Managed

First, conflicts may be solved only during a pulling. Any attempt to push while conflicts remain will be rejected.

When you pull from the server, the following conflicts may happen:

- you created a new issue identified by, say, "421", but another new issue with the same id was created on the server.
- you modified the status of issue, says, "888", but someone also modified this property on the server


### New Issue Conflict

In our example, you created a new issue identified by, say, "421", but Alice already created a new issue "421" on the server.

In this case, when you pull, Smit renames your local issue "421" to "422".

Note that only local issues may be renamed, and that issues on the server will never be renamed, so that the server is the reference.


### Same Property Conflict

In our example, you modified the status of issue, says, "888", but Alice also modified this property on the server.

If you set the same value as Alice did, then there is no conflict. Both of you did the same thing. Your change is simply removed.

But if you set a different value, then Smit prompts you to take a decision.


