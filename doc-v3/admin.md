# Administration

Most administration tasks may be performed through the web interface, and require the role superadmin.

## Create a User

To define a user's profile you must specify:

- his/her identifier (typically his/her name, or an alias) that uniquely identifies him/her
- a password (expect for some cases described below)
- permissions on the projects


## Create a Project


## Set Permissions Up

Permissions of a user on some projects specifies which role the user will have on each project.


 Role    Description 
-------  ------------
admin    Allow user to edit project properties 
rw       Allow user to create new issues and contribute on existing issues 
ro       Allow user to view issues  
ref      Do not allow user to do anything, but user appears in select-user drop-down lists 
-------  ------------

Roles on projects (a.k.a the permissions) are specified by:

- a wildcard for the project(s)
- a role

If any wildcard collisions describe more than one role on a project, then the most restrictive role applies.

Examples:

Wildcard   Role   Resulting Permissions


--------------------------------------------------------------
             Wildcard        Role       Resulting Permissions
----------   -------------   --------   ----------------------
Example 1    Cookies-*       admin      Cookies-r21: ro
             Cookies-r21     ro         Cookies-r22: admin
             Ribs-*          rw         Cookies-z3: admin
                                        Ribs-5: rw
                                        Ribs-6: rw
             
Example 2    *               admin      Cookies-r21: admin
                                        Cookies-r22: admin
                                        Cookies-z3: admin
                                        Ribs-5: rw
                                        Ribs-6: rw
---------------------------------------------------------------------

## FAQ

### Why cannot I create a project named 'public'?

`public` is reserved for storing HTML pages that are public, such as the signin page.

The reserve keywords that cannot be used as project names are:

    public
    sm
    users
    views
    .*
    issues
    files




