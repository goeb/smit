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

Verbose = False

# GNUPGHOME initialized in gnupg.d
# export GNUPGHOME=gnupg.d
# gpg --import <file>
def gpgExec(args, stdinText):
    "Execute a GNUPG command"

    # set the GNUPGHOME environment variable
    basedir = os.path.dirname(__file__)
    cmd = 'GNUPGHOME='+basedir+'/gnupg.d gpg ' + args

    if stdinText is not None :
        stdinOpt = subprocess.PIPE
    else :
        stdinOpt = None

    p = subprocess.Popen(cmd, shell=True, stdin=stdinOpt, stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True)
    if stdinText is not None :
        p.stdin.write(stdinText.encode('utf8'))
        p.stdin.close()

    p.wait()
    stdout = p.stdout.read()
    stderr = p.stderr.read()
    exitCode = p.returncode

    return exitCode, stdout, stderr


def getGpgKey(email):
    "return a GPG id of a user, if any"

    # check first if the email is known in the config
    try:
        gpgKey = MailConfig.GPG_KEYS[email]
    except:
        gpgKey = None

    if gpgKey is None :
        # check if the email is known in the GPG base
        rc, stdout, stderr = gpgExec('--list-keys ' + email, None)
	if rc == 0: gpgKey = email
    
    if Verbose: print 'getGpgKey('+email+')=%s' % (gpgKey)

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

def gpgEncrypt(clearText, addressees):
    "Encrypt text, if at least one GPG key is configured"

    gpgKeys = set()
    # check if at least one addressee has a GPG key
    for a in addressees:
	k = getGpgKey(a)
	if k is None:
            print('Missing GPG key for \'%s\'' % (a))
	else:
            gpgKeys.add(k)

    if len(gpgKeys) == 0:
        if Verbose: print "Mail in clear text"
        return text

    # build the gpg command line
    gpgArgs = '-e --trust-model always --armor'
    for k in gpgKeys:
        gpgArgs += ' -r ' + k

    exitCode, cipheredText, stderr = gpgExec(gpgArgs, clearText)
    if exitCode != 0:
        print "gpgExec error: ", stderr

    return cipheredText

def urlEscapeProjectName(pname):
    'escape characters for passing the project name in an URL'
    result = ''
    doEscape = ':?#[]@!$&"\'()*+,;=% '
    mark = '='
    for c in pname:
        if c in doEscape: result += mark + '%02x' % (ord(c))
        else: result += c
    return result

def getMailBody(jsonMsg):
    "print a recap of the properties of the issue, and the message"
    body = getMailSubject(jsonMsg) + "\r\n"
    if jsonMsg['author']: author = jsonMsg['author']
    else: author = 'unknown author'

    if jsonMsg['isNew']: body += "(new issue created by %s)" % (author)
    else: body += "(issue modified by %s)" % (author)

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
            urlEscapeProjectName(jsonMsg['project']), jsonMsg['issue'] )
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
    parser.add_argument('--if-property-modified', help='send an email if any of the given properties is modified (separated by commas)')
    parser.add_argument('--mailto-property', help='send the email to the people in the given property')
    parser.add_argument('--mailto-admins', action='store_true', help='send the email to all administrators of the project')
    parser.add_argument('--mailto', help='additionnal email addressees (separated by commas)')
    parser.add_argument('--test', action='store_true', help='test with dummy data (useful for command line debugging)')
    parser.add_argument('--verbose', action='store_true', help='be verbose')
    args = parser.parse_args()

    return args
	

# main --------------------------------------------

#print "triggers/notifyNewEntry.py(%s)" % (sys.argv)
args = parseCommandLine()

if args.verbose: Verbose = True

if args.test:
    print("Using test data...")
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
    props = args.if_property_modified.split(',')
    for p in props:
        if p in jsonMsg['modified']:
            doSendMail = True
            break
else:
    doSendMail = True

if not doSendMail:
    #print('doSendMail=False') # debug
    sys.exit(0)

# build the list of addressees
addressees = set()
if args.mailto_property:
    # send email if
    addressees = addressees.union(getMailOfProperty(jsonMsg, args.mailto_property))

if args.mailto_admins:
    # send email to admins
    addressees = addressees.union(getMailOfAdmins(jsonMsg))

if args.mailto:
    # send email to additionnal addressees
    emails = args.mailto.split(',')
    emails = [ x.strip() for x in emails ]
    addressees = addressees.union(emails)

if len(addressees) == 0:
    # no addressees, no email to send
    print('no addressees, mail not sent')
    sys.exit(0)

if Verbose: print("addressees: ", addressees)

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
