#!/usr/bin/env python

"""
Script for sending emails on new Smit entries

How to install this trigger:
    Edit the script for the settings (stmp server, emails, etc.)
        EMAILS_TO, EMAIL_FROM, SMTP_HOST
    $ cp examples/notifyNewEntry.py $REPO
    $ chmod u+x notifyNewEntry.py
    $ echo ./notifyNewEntry.py > $REPO/$PROJECT/trigger

    Example of json given on stdin:
    {
        "project": "myproject",
        "issue_id": "345",
        "entry": {
            "author": "fred",
            "id": "e74181f56c9bdcf09f3b9451a2b962ef7c67ae48",
            "properties": {
                "owner": [
                    "John Smith"
                ],
                "+message": [
                    "John, please analyse this issue. Sample code 't.c' supplied."
                ],
                "+file": [
                    "2e344bf4afba3ce778448c0bca4b7037a9487c5a/t.c"
                ]
            }
        },
        "old_issue": {
            "id": "130",
            "properties": {
                "owner": [
                    "fred"
                ],
                "parent": [],
                "summary": [
                    "segfault at startup if no space left on device"
                ],
                "status": [
                    "open"
                ]
            }
        },
        "properties_labels": {
            "summary": "Title"
        },
        "recipients": [
            {
                "email": "bob@example.com",
                "gpg_pub_key": "-----BEGIN PGP PUBLIC KEYBLOCK-----\r\nVersion: GnuPG v1\r\n\r\nmQINBE+a7rUBEADQiEKtLOgqiq8YY/p7IFODMqGPR+o1vtXaksie8iTOh3Vxab38\r\ncA3kK1iB5XYElbZ5b/x3vWiufHK2semOpn5MG2GRJUwmKxZbt3HLZiHtAadkby2l\r\n................................................................\r\n66zB8MlfnrXPgDgun9cd1IxkuK3VPjwF7PpN3QhFtjqVXQTVfOVF5JC4mhhrxZhn\r\nAp7pn3Ph7zeVhNVFkl32Gjbs3Bh4bJrvoCvhWAf9kIEnShX69NQbfZFvfG2b9Woc\r\nV5yMLH0bZ/tCa1hFRMpcqCTHsZBLcjOQhV5Ubg==\r\n=eXKL\r\n-----END PGP PUBLIC KEY BLOCK-----"
            }
        ]
    }

"""

import json
import sys
import smtplib
import tempfile
import shutil
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
def gpgExec(gnupgHome, args, stdinText):
    "Execute a GNUPG command"

    # set the GNUPGHOME environment variable
    cmd = 'gpg --homedir ' + gnupgHome + ' ' + args

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

def gpgEncrypt(clearText, gpgPublicKeys):
    '''Encrypt text with the GPG keys'''

    if len(gpgPublicKeys) == 0:
        if Verbose: print "No GPG key available"
        return ''

    # build a temporary GNUPG home directory, and populate
    tmpdir = tempfile.mkdtemp()
    ids = set()
    for k in gpgPublicKeys:
        exitCode, _, stderr = gpgExec(tmpdir, '--import', k)
        if exitCode != 0:
            print 'gpg error: ', stderr
            continue
        # parse stdout to get the key id
        for line in stderr.splitlines():
            # looking for line:
            # gpg: key BD542930: "Alice <alice@example.com>" imported
            try:
                toks = line.split(':')
                if toks[0] == 'gpg':
                    keyToks = toks[1].split()
                    if keyToks[0] == 'key':
                        ids.add(keyToks[1])
                        break # got the line, exit the line loop
            except:
                continue

    if Verbose: print 'key id: %s' % (ids)

    # cipher
    gpgArgs = '-e --trust-model always --armor'
    for i in ids:
        gpgArgs += ' -r ' + i

    exitCode, cipheredText, stderr = gpgExec(tmpdir, gpgArgs, clearText)
    if exitCode != 0:
        print "gpgExec error: ", stderr

    shutil.rmtree(tmpdir)
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

def getLabel(labels, propertyName):
    try:
        label = labels[propertyName]
    except:
        label = propertyName
    return label

def getMailBody(jsonMsg):
    "print a recap of the properties of the issue, and the message"
    body = getMailSubject(jsonMsg) + "\r\n"
    author = jsonMsg['entry']['author']
    oldIssue = jsonMsg['old_issue']
    labels = jsonMsg['properties_labels']
    entry = jsonMsg['entry']

    if oldIssue is None:
        body += '(new issue)\r\n'
        oldProperties = {}
    else:
        oldProperties = oldIssue['properties']

    body += 'Author: %s\r\n' % (author)

    body += '-' * 80 + '\r\n'
    body += 'Properties:\r\n'
    PROP_FORMAT = '%s: %s'
    for p in oldProperties:
        if p[0] == '+': continue # +message or +files
        label = getLabel(labels, p)
        oldValue = ', '.join(oldProperties[p])
        indent = 4+len(label)+2
        oldValue = oldValue.replace('\n', '\n' + indent*' ') # add indentation
        if p in entry['properties']:
            newValue = ', '.join(entry['properties'][p])
            newValue = newValue.replace('\n', '\n' + indent*' ') # add indentation
            body += '--- ' + PROP_FORMAT % (label, oldValue)
            body += '\r\n'
            body += '+++ ' + PROP_FORMAT % (label, newValue)
        else:
            body += '    ' + PROP_FORMAT % (label, oldValue)

        body += '\r\n'

    for p in entry['properties']:
        if p[0] == '+': continue # +message or +files
        if p not in oldProperties:
            label = getLabel(labels, p)
            indent = 4+len(label)+2
            newValue = ', '.join(entry['properties'][p])
            newValue = newValue.replace('\n', '\n' + indent*' ') # add indentation
            body += '+++ ' + PROP_FORMAT % (label, newValue)
            body += '\r\n'

    # message
    try:
        msg = entry['properties']['+message'][0]
    except:
        msg = ''
    
    if len(msg) > 0:
        body += '\r\n'
        body += '-' * 80 + '\r\n'
        body += 'Message:\r\n'
        body += msg
        body += '\r\n'

    # attached files
    try:
        files = entry['properties']['+file']
    except:
        files = []

    if len(files) > 0:
        body += '\r\n'
        body += '-' * 80 + '\r\n'
        body += "Attached files:\r\n"
        for f in files:
            body += "    %s\r\n" % (f)

    # link to the web server
    url = '%s/%s/issues/%s' % (WebConfig.rooturl,
            urlEscapeProjectName(jsonMsg['project']), jsonMsg['issue_id'] )
    body += '\r\n' + url + '\r\n'

    return body

def getTestData():
    return """
    {
        "project": "myproject",
        "issue_id": "345",
        "entry": {
            "author": "fred",
            "id": "e74181f56c9bdcf09f3b9451a2b962ef7c67ae48",
            "properties": {
                "owner": [
                    "John Smith"
                ],
                "+message": [
                    "John, please analyse this issue. Sample code 't.c' supplied."
                ],
                "description": [
                    "A segfault occurs when:\\n1. ...\\n2. ...\\n3. ...\\n4. four added"
                ],
                "+file": [
                    "2e344bf4afba3ce778448c0bca4b7037a9487c5a/t.c"
                ]
            }
        },
        "old_issue": {
            "id": "130",
            "properties": {
                "owner": [
                    "fred"
                ],
                "summary": [
                    "segfault at startup if no space left on device"
                ],
                "description": [
                    "A segfault occurs when:\\n1. ...\\n2. ...\\n3. ..."
                ],
                "status": [
                    "open"
                ]
            }
        },
        "properties_labels": {
            "summary": "Title"
        },
        "recipients": [
            {
                "email": "bob@example.com",
                "gpg_pub_key": null
            }
        ]
    }
"""

def parseCommandLine():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--force-ciphering', action='store_true', help='send ciphered emails, never send in clear text.')
    parser.add_argument('--test', action='store_true', help='test with dummy data (useful for command line debugging)')
    parser.add_argument('--verbose', '-v', action='store_true', help='be verbose')
    args = parser.parse_args()

    return args
	
def sendEmail(emails, subject, body):
    if Verbose: print('sendEmail to: %s' % emails)
    msg = MIMEText(body, 'plain', 'utf-8')
    msg['Subject'] = Header(subject, 'utf-8')
    msg['From'] = MailConfig.EMAIL_FROM
    msg['To'] = ";".join(emails)
    s = smtplib.SMTP(MailConfig.SMTP_HOST)
    s.sendmail(MailConfig.EMAIL_FROM, emails, msg.as_string())
    s.quit()

class Recipient:
    def __init__(self):
        self.email = None
        self.gpgPublicKey = None

    def __str__(self):
        s = '%s/' % self.email
        if self.gpgPublicKey: s += 'GPG'
        else: s += 'no-gpg'
        return s

    def __repr__(self):
        return self.__str__()

def getRecipients(jsonMsg):
    try:
        jsonRecipients = jsonMsg['recipients']
    except:
        return []

    recipients = []
    for jsr in jsonRecipients:
        r = Recipient()
        try:
            r.email = jsr['email']
        except:
            continue

        try:
            r.gpgPublicKey = jsr['gpg_pub_key']
            if r.gpgPublicKey.strip() == '': r.gpgPublicKey = None
        except:
            pass # because gpgPublicKey is optional

        recipients.append(r)

    return recipients

def getSummary(jsonMsg):
    try:
        summary = jsonMsg['entry']['properties']['summary'][0]
    except:
        try:
            summary = jsonMsg['old_issue']['properties']['summary'][0]
        except:
            summary = 'undefined'

    return summary

def getMailSubject(jsonMsg):
    project = jsonMsg['project']
    issueId = jsonMsg['issue_id']
    summary = getSummary(jsonMsg)
    subject = '[%s] %s: %s' % (project, issueId, summary)
    return subject

def main():
    global Verbose
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

    recipients = getRecipients(jsonMsg)

    if len(recipients) == 0:
        print('no recipient, mail not sent')
        sys.exit(0)

    if Verbose: print("recipients: %s" % recipients)

    # set contents of the email
    subject = getMailSubject(jsonMsg)
    body = getMailBody(jsonMsg)

    doCipher = True # true if all recipients have a GPG key
    for r in recipients:
        if r.gpgPublicKey is None:
            doCipher = False
            break
            
    emails = [r.email for r in recipients]
    if Verbose: print('doCipher=%s, emails=%s' % (doCipher, emails))

    if doCipher or args.force_ciphering:
        gpgPublicKeys = set()
        for r in recipients:
            if r.gpgPublicKey is not None:
                if Verbose: print 'add gpgPublicKey for', r.email
                gpgPublicKeys.add(r.gpgPublicKey)

        if len(gpgPublicKeys):
            cipheredBody = gpgEncrypt(body, gpgPublicKeys)
            # Send ciphered email
            sendEmail(emails, subject, cipheredBody)

    else:
        # send in clear text
        sendEmail(emails, subject, body)


main()
