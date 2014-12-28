

# Example:

    ./notifyNewEntry.py --mailto-property assignee --test --if-property-modified assignee

Assuming that the project has a property named 'assignee', this will send an email:
    - is the assignee has been modified
    - to the new assignee   


# Initialize a GnuPG keyring for using public keys of addressees:

    export GNUPGHOME=gnupg.d
    gpg --import <file>


