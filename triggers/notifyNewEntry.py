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

import json
import sys
import smtplib
import time
import os
from email.mime.text import MIMEText
from email.header import Header
import subprocess

import MailConfig
import WebConfig

def getGpgKey(email):
    "return the GPG id of a user, if any"
    try:
        gpgKey = MailConfig.GPG_KEYS[email]
    except:
        gpgKey = None
    
    #print 'getGpgKey('+email+')=%s' % (gpgKey)
    return gpgKey

def getEmail(username):
    "return the email of a user"
    try:
        email = MailConfig.EMAILS_TO[username]
    except:
        email = None
    
    return email

def getMailOfProperty(jsonMsg, propertyName):
    "return email addresses of people designated in the given property"
    addressees = set()
    people = getPropertyValue(jsonMsg, propertyName)
    if isinstance(people, list):
        for p in people:
            email = getEmail(p)
            if email: addressees.add(email)
    else:
        email = getEmail(people)
        if email: addressees.add(email)

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
def gpgEncrypt(text, addressees):
    "Encrypt text, if at least one GPG key is configured"
    basedir = os.path.dirname(__file__)
    gpgKeys = set()
    for a in addressees:
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

def escapeProjectName(pname):
    'escape characters except alphanumerics and ._-'
    result = ''
    dontEscape = '._-'
    mark = '='
    for c in pname:
        if c.isalnum(): result += c
        elif c in dontEscape: result += c
        else: result += mark + '%02x' % (ord(c))
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
        #print("files=%s" % files)
        if len(files)>0:
            body += "---- attached files -------------\r\n"
            for f in files:
                body += "    %s\r\n" % (f)
    except:
        pass
    # link to the web server
    url = '%s/%s/issues/%s' % (WebConfig.rooturl,
            escapeProjectName(jsonMsg['project']), jsonMsg['issue'] )
    body += '\r\n' + url + '\r\n'

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
  "homer":"admin",
  "alice":"rw"},
"modified":["type", "assignee"],
"properties":{
  "assignee":["assignee","homer"],
  "description":["Description",""],
  "status":["status","closed"],
  "summary":["summary","test notification"]
},
"message":"foo...."
}
"""

def parseCommandLine():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--if-property-modified', help='send an email if the given property is modified')
    parser.add_argument('--mailto-property', help='send the email to the people in the given property')
    parser.add_argument('--mailto-admins', action='store_true', help='send the email to all administrators of the project')
    parser.add_argument('--test', action='store_true', help='test with dummy data (useful for command line debugging)')
    args = parser.parse_args()

    #print "--if-property-modified", args.if_property_modified
    #print "--mailto-property", args.mailto_property
    #print "--test", args.test
    return args
	

# main --------------------------------------------

#print "triggers/notifyNewEntry.py(%s)" % (sys.argv)
args = parseCommandLine()

if args.test:
    raw = getTestData()
else:
    raw = sys.stdin.read()

try:
    jsonMsg = json.loads(raw)
except:
    print("error in json.loads")
    print("raw=>>>\n%s\n<<<\n" % (raw))
    sys.exit(1);

# check the conditions for sending the email
doSendMail = False
if args.if_property_modified:
    if args.if_property_modified in jsonMsg['modified']:
        doSendMail = True
else:
    doSendMail = True

if not doSendMail:
    print('doSendMail=False') # debug
    sys.exit(0)

# build the list of addressees
addressees = set()
if args.mailto_property:
    # send email if
    addressees = addressees.union(getMailOfProperty(jsonMsg, args.mailto_property))

if args.mailto_admins:
    # send email to admins
    addressees = addressees.union(getMailOfAdmins(jsonMsg))

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
msg['From'] = MailConfig.EMAIL_FROM
msg['To'] = ";".join(addressees)
s = smtplib.SMTP(MailConfig.SMTP_HOST)
s.sendmail(MailConfig.EMAIL_FROM, addressees, msg.as_string())
s.quit()
