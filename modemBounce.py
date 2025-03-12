#!/usr/bin/python

import os
import sys
import requests
import time
import datetime
import subprocess

progname = "modemBounce.py"
shellyBaseurl = 'http://10.0.0.212'


def nowstr():
    fmt = '%Y-%b-%d %H:%M:%S'
    return datetime.datetime.today().strftime('%Y-%b-%d %H:%M:%S')


def sendEmail():
    print("WARNING(%s): connectivity failure detected; preparing an email..." % (nowstr()))
    filename = "/tmp/"+progname+"-msg.txt"
    f = open(filename, "w")
    f.write("To: j.cicchiello@ieee.org\n")
    print("INFO(%s): To: j.cicchiello@ieee.org" % (nowstr()))
    f.write("From: jcicchiello@ptd.net\n")
    print("INFO(%s): From: jcicchiello@ptd.net" % (nowstr()))
    f.write("Subject: ALERT: %s\n" % progname)
    print("INFO(%s): Subject: ALERT %s" % (nowstr(), progname))
    f.write("\n")
    print("INFO(%s): " % (nowstr()))
    f.write("INFO(%s): %s has detected, and recovered from, a connectivity failure!\n" % (nowstr(), progname))
    print("INFO(%s): %s has detected, and recovered from, a connectivity failure!" % (nowstr(), progname))
    f.write("\n")
    print("INFO(%s): " % (nowstr()))
    f.write("\n")
    print("INFO(%s): " % (nowstr()))
    f.close()
    time.sleep(5)
    with open(filename, 'r') as infile:
        subprocess.Popen(['/usr/bin/msmtp', 'j.cicchiello@gmail.com'],
                         stdin=infile, stdout=sys.stdout, stderr=sys.stderr)

        
def bounce():
    print("WARNING(%s): Turning off the modems..." % nowstr())
    
    # Turn off the modem
    requests.get('%s/rpc/Switch.Set?id=0&on=false' % shellyBaseurl)
    
    # .. wait a bit
    print("WARNING(%s): waiting 15s..." % nowstr())
    time.sleep(15)
    
    print("WARNING(%s): turning modems back on..." % nowstr())
    
    # Turn on the modem
    requests.get('%s/rpc/Switch.Set?id=0&on=true' % shellyBaseurl)
    
    # wait 210s to (hopefully) fully come back online, then send an alert email
    # (Note shorter waits didn't work reliably)
    print("WARNING(%s): waiting 210s..." % nowstr())
    time.sleep(210)
    
    print("WARNING(%s): sending alert email..." % nowstr())
    sendEmail()
    time.sleep(30)

    

def test(url, triesLeft):
    try: 
        response = requests.get(url)
 
        #print("Status Code", response.status_code)
        print("INFO(%s): Connectivity test passed" % (nowstr()))

    except:
        print("WARNING(%s): Connectivity test failed (%d); pausing for one minute..." % (nowstr(), triesLeft))
        if triesLeft > 0:
            time.sleep(60)
            test(url, triesLeft-1)
        else:
            # all tries exhausted...   have to bounce the modem
            print("WARNING(%s): All tries exhausted..." % nowstr())
            bounce()


if __name__ == "__main__":
    url = "https://google.com"
    progname=os.path.basename(sys.argv[0])
    verbose = False
    if (len(sys.argv) > 1) and (sys.argv[1] == '-t1'):
        url = "https://googlefoo.com"
        print("INFO(%s): %s testing with non-existent url: %s" % (nowstr(), progname, url))
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-t2'):
        print("INFO(%s): %s testing: forcing bounce..." % (nowstr(), progname))
        bounce()
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-e'):
        print("INFO(%s): %s testing email delivery" % (nowstr(), progname))
        sendEmail()
    else:
        if verbose: 
            print("INFO(%s): %s running with url: %s" % (nowstr(), progname, url))

    test(url, 4) # 4 retries means 5 tries (1 minute per try)
