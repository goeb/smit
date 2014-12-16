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
SMTP_HOST = 'smtp.example.com',


import json
import sys
import smtplib
import time
from email.mime.text import MIMEText
from email.header import Header

def getEmail(username):
    "return the email of a user"
    try:
        email = EMAILS_TO[username]
    except:
        email = None
    
    return email

def getMailOfNewAssignee(jsonMsg, assigneePropertyName):
    "return emails of people newly assigned on the issue (it makes sense only if the project has a dedicated property)"
    if assigneePropertyName in jsonMsg['modified']:
        adressees = []
        newAssignee = getPropertyValue(jsonMsg, assigneePropertyName)
        email = getEmail(newAssignee)
        if email is not None:
            adressees.append(email)

    return adressees

def getMailOfAdmins(jsonMsg):
    "return emails of the administrators of the project"
    adressees = []
    users = jsonMsg["users"]
    for u in users:
        if users[u] == "admin":
            email = getEmail(u)
            if email is not None:
                adressees.append(email)

    # remove duplicates
    adressees = list(set(adressees))
    return adressees

def getPropertyLabel(jsonMsg, propertyName):
    try:
        label = jsonMsg['properties'][propertyName][0]
    except:
        label = propertyName
    return label

def getPropertyValue(jsonMsg, propertyName):
    try:
        v = jsonMsg['properties'][propertyName][1]
    except:
        v = None
    return v

def getMailSubject(jsonMsg):
    s = "[%s] %s: %s" % (jsonMsg['project'], jsonMsg['issue'], getPropertyValue(jsonMsg, 'summary'))
    return s

def getMailBody(jsonMsg):
    "print a recap of the properties of the issue, and the message"
    body = getMailSubject(jsonMsg) + "\r\n"
    body += "---- properties ------------\r\n"
    for p in jsonMsg['properties']:
        if p in jsonMsg['modified']: body += '** '
        else: body += '   '
        body += "%-25s: " % (getPropertyLabel(jsonMsg, p))
        value = "%s" % (getPropertyValue(jsonMsg, p))
        body += '%s' % (value)
        body += "\r\n"

    # message
    msg = jsonMsg['message']
    if len(msg) > 0:
        body += "---- message ----------------\r\n"
        body += msg
        body += '\r\n'

    # attached files
    try:
        files = jsonMsg['files']
        print("files=%s" % files)
        if len(files)>0:
            body += "---- attached files ---------\r\n"
            for f in files:
                body += "    %s\r\n" % (f)
    except:
        pass

    return body

# main --------------------------------------------

raw = sys.stdin.read()
try:
    jsonMsg = json.loads(raw)
except:
    print("error in json.loads")
    print("raw=>>>\n%s\n<<<\n" % (raw))
    sys.exit(1);


adressees = getMailOfAdmins(jsonMsg)
if len(adressees) == 0:
    # no addressees, no email to send
    sys.exit(0)

# set contents of the email
subject = getMailSubject(jsonMsg)
body = getMailBody(jsonMsg)
msg = MIMEText(body, 'plain', 'utf-8')
msg['Subject'] = Header(subject, 'utf-8')
msg['From'] = EMAIL_FROM
msg['To'] = ";".join(adressees)
s = smtplib.SMTP(SMTP_HOST)
s.sendmail(EMAIL_FROM, adressees, msg.as_string())
s.quit()
