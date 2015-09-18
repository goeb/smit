# Developers' Guide

## Software Architecture

Smit:

- is built on top of [Mongoose HTTP server](http://code.google.com/p/mongoose/)
- relies on OpenSSL for TLS authentication and encryption


## Directories and files

The database is made of directories and files.
The layout is as follows:

    $REPO
     ├── project-A
     │   └── .smip
     │       ├── objects/
     │       ├── refs
     │       │   ├── issues/
     │       │   ├── project
     │       │   ├── tags/
     │       │   ├── trigger
     │       │   └── views
     │       └── templates/
     ├── project-B/
     ├── .../
     ├── public/
     └── .smit
         ├── templates/
         └── users
             ├── auth
             └── permissions
 


An issue is made up of successive immutable entries. The lastest entry is referenced by `.smip/refs/issues/<id>` (where `<id>` is the identifier of the issue).

### Sample entry of an issue

Entries are text files with straightforward key-value syntax.

    +parent null
    +author Fred
    +ctime 1379878590
    +message < -----------endofmsg---
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor
    incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis
    nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.
    Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu
    fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in
    culpa qui officia deserunt mollit anim id est laborum."
    -----------endofmsg---
    +file a8fdc205a9f19cc1c7507a60c4f01b13d11d7fd0/a_file.txt
    status open
    target_version v1.0
    summary "Lorem ipsum dolor sit amet"

Keys with a `+` sign are specific to the entry and are not taken for the consolidation of the issue properties.

### File `project`

This file describes the structure of the properties of the project.

    setPropertyLabel id "#"
    setPropertyLabel ctime Created
    setPropertyLabel mtime Modified
    setPropertyLabel summary Description
    addProperty status -label "The status" select open closed deleted
    addProperty target_version select v1.1 v1.2 v2.0 other
    addProperty owner selectUser

### File `users/auth`

This file describes how the users authenticate.

    adduser "John Smith" -type sha1 -hash e61a3587b3f7a142b8c7b9263c82f8119398ecb7 \
        -salt b3f7a1
    adduser alice -type krb5 -realm EXAMPLE.COM
    adduser homer -type ldap -uri ldaps://example.com:389 \
        -dname "cn=homer,ou=People,dc=example,dc=com"


### File `users/permissions`

This file describes the permissions of the users.

Example:

    setperm "John Smith" rw project-*
    setperm "John Smith" superadmin
    setperm "John Smith" ro project-B
    setperm alice rw project-A


### Directory `public`

This directory contains public material, such as logo, `style.css`, and any contents that you wish to make available publicly.

## Objects

`objects` is a database that stores:

- the entries
- the attached files
- the configuration of the project
- the views

The file name of an object is based on the SHA1 sum of its contents. Example : 

    sha1sum a_file.txt
    a8fdc205a9f19cc1c7507a60c4f01b13d11d7fd0 a_file.txt

This file shall be stored at this location:

    .smip/objects/a8/fdc205a9f19cc1c7507a60c4f01b13d11d7fd0

The original names of the files are not known in the `objects` database layout. The names of attached files are kept in the entries that reference the files (see the example of an entry, above).
