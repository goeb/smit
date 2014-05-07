mkdir IssuesRepo
smit.exe init IssuesRepo
smit.exe project -c -d IssuesRepo SampleProject
smit.exe user admin -d IssuesRepo --superadmin --passwd admin --project SampleProject admin
