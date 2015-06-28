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




