mkdir IssuesRepo
smit.exe init IssuesRepo
smit.exe addproject -d IssuesRepo SampleProject
smit.exe adduser admin -d IssuesRepo --superadmin --passwd admin --project SampleProject admin
