#!/usr/bin/env python

"""
Script for sending emails on new Smit entries

How to install this trigger:
    Edit the script for the settings (stmp server, emails, etc.)
        EMAILS_TO, EMAIL_FROM, SMTP_HOST
    $ cp examples/notifyNewEntry.py $REPO
    $ chmod u+x notifyNewEntry.py
    $ echo ./notifyNewEntry.py > $REPO/$PROJECT/trigger

"""

EMAILS_TO = {
    'smit-user-1' : 'smit-user-1@example.com',
    'smit-user-2' : 'smit-user-2@example.com',
    'smit-user-3' : 'smit-user-3@example.com'
}
EMAIL_FROM = 'smit@example.com'
SMTP_HOST = 'smtp.example.com'


import json
import sys
import smtplib
import time
from email.mime.text import MIMEText

def getEmail(username):
    return EMAILS_TO[username]

raw = sys.stdin.read()
data = json.loads(raw)

adressees = []
users = data["users"]
for u in users:
    if users[u] == "admin": adressees.append(getEmail(u))

# remove duplicates
adressees = list(set(adressees))

# set contents of the email
msg = MIMEText(raw)

msg['Subject'] = "Issue " + data["issue"] + ": " + data["properties"]["summary"][1]

msg['From'] = EMAIL_FROM
msg['To'] = ";".join(adressees)
s = smtplib.SMTP(SMTP_HOST)
s.sendmail(EMAIL_FROM, adressees, msg.as_string())
s.quit()
