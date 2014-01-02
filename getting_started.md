# Getting Started

## Online demo

An online demo is available at this address: [http://smit.herokuapp.com](http://smit.herokuapp.com)

## Getting Started on Linux

    git clone https://github.com/goeb/smit.git
    cd smit
    make
    ./smit serve demo &

Then, browse to: [http://127.0.0.1:8090/things_to_do/issues](http://127.0.0.1:8090/things_to_do/issues)


## Getting Started on Windows

Unzip the smit package, and run:

    init.bat
    start.bat

By default these scripts initiate a repository with a superadmin with username `admin` and password `admin`.

### init.bat
There is what `init.bat` looks like:

    REM create a new empty directory
    mkdir IssuesRepo 

    REM Initiate this directory as a Smit repository
    smit.exe init IssuesRepo

    REM Create in this repository a project named "SampleProject"
    smit.exe addproject -d IssuesRepo SampleProject

    REM Create in this repository a user, that has administrator priviledges
    smit.exe adduser admin -d IssuesRepo --superadmin --passwd admin --project SampleProject admin
    
### start.bat
There is the `start.bat` script:

    REM Start the Smit server locally
    start /B smit serve IssuesRepo --listen-port 8090

    REM Start your favorite browser
    start http://127.0.0.1:8090

