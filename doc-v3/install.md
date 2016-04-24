# Installation

## Get the Software

### Windows Platform

- Unzip
- `smit.exe` is ready to use

### Linux

Requirements:

- OpenSSL
- Libcurl

Installation instructions:

    git clone http://github.com/goeb/smit
    cd smit
    make
    make check
    make install

And copy the compiled executable `smit` to your PATH (for example /usr/bin).

## Create a Repository

```
REPO=/path/to/some/dir
mkdir $REPO
smit init $REPO
```

## Start a Smit web server

```
smit serve
```

## Setup a Smit web server over SSL/TLS

If you do not have a certificate yet, create a self-signed certificate:

```
openssl genrsa > privkey.pem
openssl req -new -x509 -key privkey.pem -out cacert.pub.pem -days 1095
cat privkey.pem cacert.pub.pem > cacert.pem
```

Start Smit:

```
smit serve --ssl-cert cacert.pem
```

To redirect clients from a non-secured port 8090 to HTTPS on port 8091:

```
smit serve --ssl-cert cacert.pem --listen-port 8090r,8091s
```



## Set up a trigger

A trigger defines an external program to be launched after each new entry. It is typically useful for sending email notifications when some condition occur.

Triggers are not supported on Windows.

The file `trigger` in a Smit project defines the path to the external program, on the first line.

Example:

```
$ cat $REPO/project-X1/.smip/refs/trigger
notifyNewEntry.py
```


Notes:

- if the path is relative, it is considered relatively to the Smit repository
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

An example of trigger program is given in the "triggers" directory.

## Make a Backup

Do a zip or tar of the repository, as follows:

```
tar cvfz $REPO.tar.gz $REPO
```

## Setup behind a reverse proxy

Use the `--url-rewrite-root` option. Eg:

```
linux/smit serve demo --url-rewrite-root /bt
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

In this example, Smit is available at address: `http://127.0.0.1:8092/bt/`

