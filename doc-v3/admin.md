# Administration

Most administration tasks may be performed through the web interface, and require the role superadmin.

## Create a User

To define a user's profile you must specify:

- his/her identifier (typically his/her name, or an alias) that uniquely identifies him/her
- a password (expect for some cases described below)
- permissions on the projects


## Create a Project

Project names cannot contain reserved "directory" parts.

The "directory" parts are those separated by slashes '/'. The reserved parts are:

- `config`, `files`, `issues`, `public`, `reload`, `sm`, `tags`, `users`, `views`
- any name starting with a dot '.'

Examples of forbidden project names:

- some-tech-project/public
- .cook
- kitchen/.cook


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

In the examples below, the Smit repository has 3 projects:

- Cookies-A
- Cookies-B
- Ribs-6

### Example 1

```
Wildcards                 Resulting Permissions
-----------------------------------------------
Cookies-* : admin         Cookies-A : admin
Cookies-B : ro            Cookies-B : ro
                             Ribs-6 : none
```

### Example 2

```
Wildcards                 Resulting Permissions
-----------------------------------------------
* : rw                    Cookies-A : rw
                          Cookies-B : rw
                             Ribs-6 : rw
```





