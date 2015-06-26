# Administration

The commands given in this chapter are to be issued in a shell in a Linux OS. For a Windows Os, they may need some adaptation.

## Initiate a Repository

```
REPO=/path/to/some/dir
mkdir $REPO
smit init $REPO
```

## Create a Project

```
cd $REPO
smit project -c <project-name>
smit user <user-name> --passwd <pass>
smit user <user-name> --project <project-name>:admin
```

## Start a smit web server

```
smit serve
```

## Setup a smit web server over SSL/TLS

Create a self-signed certificate:

```
openssl genrsa > privkey.pem
openssl req -new -x509 -key privkey.pem -out cacert.pub.pem -days 1095
cat privkey.pem cacert.pub.pem > cacert.pem
```

Start smit:

```
smit serve --ssl-cert cacert.pem
```



## Smit Repository Layout

(TODO)

## Project Configuration

The project configuration defines:

- the properties of the issues
- the tags
- the numbering scheme of the issues (local or global to several projects)

The configuration may be modified in two ways:

- via the web interface
    - only the properties may be managed this way
    - hot reload 
- via editing directly the configuration file, and performing a hot reload via the web interface

The configuration of a project is given by the file `project` in the directory of the project. 

### addProperty
```
addProperty <id> [-label <label>] [-help <help>] <type> [values ...]
```

`addProperty` defines a property.

- `<id>` is an identifier (only characters a-z, A-Z, 0-9, -, _)
- `<label>` is the text that will be displayed in the HTML pages (optional)
- `<help>` is a tooltip for properties inputs, displayed above properties labels (optional)
- `<type>` is one of text, select, multiselect, selectUser, textarea, textarea2

    * `text`: free text
    * `select`: selection among a list if given values
    * `multiselect`: same as select, but several may be selected at the same time
    * `selectUser`: selection among the users of the project
    * `textarea`: free text, multi-lines
    * `textarea2`: same as textarea, but spanned on 2 columns in the HTML
    
- `value` indicates the allowed values for types select and multiselect.

### setPropertyLabel

```
setPropertyLabel <propety-id> <label>
```

`setPropertyLabel` defines the label for a property. This is used for mandatory properties that are not defined by `addProperty`: id, ctime, mtime, summary.

### numberIssues

```
numberIssues global
```

`numberIssues` defines the numbering policy of the issues.

If not defined, the issues are numbered reletively to their project: 1, 2, 3,...

If `global` is set, then the numbering is shared by all the projects that have this policy.


### tag

Entries may be tagged.

```
tag <id> -label <text> [-display]
```

- `<id>` may contain only letters, digits and underscores.
- `<text>` is the text to display in the HTML page
- `-display` requires the display of a box in the headers of issues, that indicates if at least one entry of the current issue is tagged (this may be seen as a checkbox, to quickly verify that some quality criterion is met).


### Full example 

```
setPropertyLabel id "The ID"

addProperty status -label "The Status" select open closed
addProperty owner -label "The owner" selectUser

numberIssues global

tag test -label "Test Proof" -display

trigger public/sendEmail.sh
```

## Set up a trigger

A trigger defines an external program to be launched after each new entry. It is typically useful for sending email notifications when some condition occur.

Triggers are not supported on Windows.

The file `trigger` in a smit project defines the path to the external program, on the first line.

Example:

```
$ cat $REPO/project-X1/trigger
notifyNewEntry.py
```


Notes:

- if the path is relative, it is considered relatively to the smit repository
- the external program must be executable

On creation or modification of an issue, the trigger will be called, and passed a JSON structure on its standard input, like this example:

```
{
"project":"myproject",
"issue":"13",
"entry":"ed3eda2976914998cf2fcd759adf71753d0aa5f8",
"author":"fred",
"users":{
  "fred":"admin",
  "not assigned":"ref",
  "xxxt":"rw"},
"modified":["a-b-c","multi-a","new-ttt","owner","test-reload","textarea2","xx"],
"properties":{
  "a-b-c":["a-b-xx66",""],
  "multi-a":["%multi-h","42"],
  "new-ttt":["new-ttt",""],
  "owner":["owner'44r%","fred"],
  "summary":["summary","fatal error x8"],
  "target_version":["target_version\"22","v0.1"],
  "test-reload":["test-reloadx",""],
  "textarea2":["textarea2",""],
  "xx":["xx(yy)__99",""]
},
"message":"..."
}
```

Example of trigger program is given: [notifyNewEntry.py](../downloads/notifyNewEntry.py)

## Make a Backup

Do a zip or tar of the repository, as follows:

```
tar cvfz $REPO.tar.gz $REPO
```

## Setup behind a reverse proxy

Use the `--url-rewrite-root` option. Eg:

```
linux/smit serve demo --url-rewrite-root /bugtracker
```

Example of reverse proxy configuration with lighttpd:
(tested with lighttpd-1.4.35)

```
server.document-root = "/tmp"
server.port = 3000
server.modules += ( "mod_proxy" , "mod_rewrite")

$SERVER["socket"] == ":8092" {
    url.rewrite-once = ( "^/bt/(.*)$" => "/$1" )
    proxy.server = ( "" => ( (
        "host" => "127.0.0.1",
        "port" => 8090
    ) )
    )
}

```

In this example, smit is available at address: `http://127.0.0.1:8092/bt/`

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




