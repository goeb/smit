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
GPG_KEYS = {
        'smit-user-3@example.com' : '01020304'
}

EMAIL_FROM = 'smit@example.com'
SMTP_HOST = 'smtp.example.com',


import json
import sys
import smtplib
import time
import os
from email.mime.text import MIMEText
from email.header import Header
import subprocess

def getGpgKey(email):
    "return the GPG id of a user, if any"
    try:
        gpgKey = GPG_KEYS[email]
    except:
        gpgKey = None
    
    #print 'getGpgKey('+email+')=%s' % (gpgKey)
    return gpgKey

def getEmail(username):
    "return the email of a user"
    try:
        email = EMAILS_TO[username]
    except:
        email = None
    
    return email

def getMailOfNewAssignee(jsonMsg, assigneePropertyName):
    "return emails of people newly assigned on the issue (it makes sense only if the project has a dedicated property)"
    addressees = set()
    if assigneePropertyName in jsonMsg['modified']:
        newAssignee = getPropertyValue(jsonMsg, assigneePropertyName)
        email = getEmail(newAssignee)
        if email is not None:
            addressees.add(email)
    #else:
    #    print('not \'%s\' in: %s' % (assigneePropertyName, jsonMsg['modified']))

    return addressees

def getMailOfAdmins(jsonMsg):
    "return emails of the administrators of the project"
    addressees = set()
    users = jsonMsg["users"]
    for u in users:
        if users[u] == "admin":
            email = getEmail(u)
            if email is not None:
                addressees.add(email)

    return addressees

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

# GNUPGHOME initialized in gnupg.d
# export GNUPGHOME=gnupg.d
# gpg --import <file>
def gpgEncrypt(text, addresees):
    "Encrypt text, if at least one GPG key is configured"
    basedir = os.path.dirname(__file__)
    gpgKeys = set()
    for a in addresees:
	k = getGpgKey(a)
	if k is None:
            print('Missing GPG key for \'%s\'' % (a))
	else:
            gpgKeys.add(k)

    if len(gpgKeys) == 0: return text

    # build the gpg command line
    cmd = 'GNUPGHOME='+basedir+'/gnupg.d gpg -e --trust-model always --armor'
    for k in gpgKeys:
        cmd += ' -r ' + k

    #print 'cmd=', cmd
    p = subprocess.Popen(cmd, shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE, close_fds=True)
    p.stdin.write(text.encode('utf8'))
    p.stdin.close()
    result = p.stdout.read()
    #print "result=", result
    return result


def getMailBody(jsonMsg):
    "print a recap of the properties of the issue, and the message"
    body = getMailSubject(jsonMsg) + "\r\n"
    if jsonMsg['isNew']: body += "(new issue created)"
    else: body += "(issue modified)"
    body += '\r\n'
    body += "---- properties -------------------------\r\n"
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
        body += "---- message ------------------------\r\n"
        body += msg
        body += '\r\n'

    # attached files
    try:
        files = jsonMsg['files']
        print("files=%s" % files)
        if len(files)>0:
            body += "---- attached files -------------\r\n"
            for f in files:
                body += "    %s\r\n" % (f)
    except:
        pass

    return body

def getTestData():
    return """
{
"project":"project-x\\/sw",
"issue":"4357",
"isNew":false,
"entry":"34588ff8b582016f1f29ce8e531fbab9996fbc5a",
"author":"homer",
"users":{
  "hoerni":"admin",
  "homer":"rw"},
"modified":["type", "assignee"],
"properties":{
  "assignee":["assignee","hoerni"],
  "description":["Description",""],
  "status":["status","closed"],
  "summary":["summary","test notification"]
},
"message":"foo...."
}
"""

def parseCommandLine():
    testMode = False
    notificationPolicy = None
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--mail-assignee-if-changed', nargs=1)
    parser.add_argument('--test', nargs='?')
    args = parser.parse_args()

    if hasattr(args, 'test'): testMode = True
    if hasattr(args, 'mail_assignee_if_changed'):
	notificationPolicy = 'mail_assignee_if_changed'
	notificationPolicyOpt = args.mail_assignee_if_changed[0]

    return (testMode, notificationPolicy, notificationPolicyOpt)
	

# main --------------------------------------------

print "triggers/notifyNewEntry.py(%s)" % (sys.argv)
testMode, notificationPolicy, notificationPolicyOpt = parseCommandLine()

if testMode:
    raw = getTestData()
else:
    raw = sys.stdin.read()

try:
    jsonMsg = json.loads(raw)
except:
    print("error in json.loads")
    print("raw=>>>\n%s\n<<<\n" % (raw))
    sys.exit(1);

# if option --mail-assignee-if-changed is set, then send the email to the person
if notificationPolicy == 'mail_assignee_if_changed':
    addressees = getMailOfNewAssignee(jsonMsg, notificationPolicyOpt)
else:
    # email to admins
    addressees = getMailOfAdmins(jsonMsg)

if len(addressees) == 0:
    # no addressees, no email to send
    print('no addressees')
    sys.exit(0)

# set contents of the email
subject = getMailSubject(jsonMsg)
body = getMailBody(jsonMsg)
body = gpgEncrypt(body, addressees)
msg = MIMEText(body, 'plain', 'utf-8')
msg['Subject'] = Header(subject, 'utf-8')
msg['From'] = EMAIL_FROM
msg['To'] = ";".join(addressees)
s = smtplib.SMTP(SMTP_HOST)
s.sendmail(EMAIL_FROM, addressees, msg.as_string())
s.quit()
