#!/usr/bin/python

import os
import sys
import requests
import time
import datetime
import subprocess
import socket

progname = "modemBounce.py"

Internet = "8.8.8.8"
DNS = "google.com"
Modem = "192.168.100.1"
Router = "10.0.0.1"

shellyBaseurl = 'http://10.0.0.212'


def usage():
    print("Usage: ")
    print("> modemBounce [-tDNS -tROUTER -tIP -tMODEM -tBOUNCE-MODEM -tBOUNCE-ROUTER]")
    print("")
    print("where")
    print("   -tDNS\t\ttest DNS failure")
    print("   -tROUTER\t\ttest Router failure")
    print("   -tIP\t\t\ttest IP failure")
    print("   -tMODEM\t\ttest Modem failure")
    print("   -tBOUNCE-MODEM\ttest the modem bounce")
    print("   -tBOUNCE-ROUTER\ttest the router bounce")
    
    

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

        
def bounceModem():
    print("WARNING(%s): Turning off the modem..." % nowstr())
    
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


def bounceRouter():
    print("WARNING(%s): Turning off the router..." % nowstr())


def dns_works(hostname):
    try:
        socket.gethostbyname(hostname)
        return True
    except socket.gaierror:
        return False


def ping(host, count=3, timeout_sec=2):
    """
    Return True if host responds to ping, False otherwise.
    Linux/Raspberry Pi version.
    """
    result = subprocess.run(
        [
            "ping",
            "-c", str(count),        # number of pings
            "-W", str(timeout_sec),  # timeout per ping, seconds
            host,
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def test(router, modem, internet, dns, triesLeft):
    #print("TRACE(test): router(%s), modem(%s), internet(%s), dns(%s), triesLeft(%d)" % \
    #      (router, modem, internet, dns, triesLeft))
    if not ping(router):
        print("WARNING(%s): Router test failed (%d); pausing for one minute..." % (nowstr(), triesLeft))
        if triesLeft > 0:
            time.sleep(60)
            test(router, modem, internet, dns, triesLeft-1)
        else:
            # all tries exhausted...   have to bounce the router
            print("WARNING(%s): All router tries exhausted; bouncing router..." % nowstr())
            bounceRouter()
    elif not ping(modem):
        print("WARNING(%s): Modem test failed (%d); pausing for one minute..." % (nowstr(), triesLeft))
        if triesLeft > 0:
            time.sleep(60)
            test(router, modem, internet, dns, triesLeft-1)
        else:
            # all tries exhausted...   have to bounce the modem
            print("WARNING(%s): All modem tries exhausted; bouncing modem..." % nowstr())
            bounceModem()
    elif not ping(internet):
        print("WARNING(%s): Internet test failed (%d); pausing for one minute..." % (nowstr(), triesLeft))
        if triesLeft > 0:
            time.sleep(60)
            test(router, modem, internet, dns, triesLeft-1)
        else:
            # all tries exhausted...   have to bounce the modem
            print("WARNING(%s): All internet tries exhausted; bouncing modem..." % nowstr())
            bounceModem()
    elif not dns_works(dns):
        print("WARNING(%s): DNS test failed (%d); pausing for one minute..." % (nowstr(), triesLeft))
        if triesLeft > 0:
            time.sleep(60)
            test(router, modem, internet, dns, triesLeft-1)
        else:
            # all tries exhausted...   have to bounce the modem
            print("WARNING(%s): All DNS tries exhausted; bouncing modem..." % nowstr())
            bounceModem()
    else:
        # healthy
        print("INFO(%s): Connectivity test passed" % (nowstr()))




if __name__ == "__main__":
    router = Router;
    internet = Internet;
    dns = DNS;
    modem = Modem;
    
    progname=os.path.basename(sys.argv[0])
    verbose = False
    if (len(sys.argv) > 1) and (sys.argv[1] == '-tDNS'):
        dns = dns+"foo"
        print("INFO(%s): %s testing with non-existent dns: %s" % (nowstr(), progname, dns))
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-tROUTER'):
        router = router + "11"
        print("INFO(%s): %s testing with non-existent router ip: %s" % (nowstr(), progname, router))
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-tIP'):
        internet = internet + "1"
        print("INFO(%s): %s testing with non-existent IP: %s" % (nowstr(), progname, internet))
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-tMODEM'):
        modem = modem + "11"
        print("INFO(%s): %s testing with non-existent modem IP: %s" % (nowstr(), progname, modem))
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-tBOUNCE-MODEM'):
        print("INFO(%s): %s testing: forcing bounce..." % (nowstr(), progname))
        bounceModem()
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-tBOUNCE-ROUTER'):
        print("INFO(%s): %s testing: forcing bounce..." % (nowstr(), progname))
        bounceRouter()
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-e'):
        print("INFO(%s): %s testing email delivery" % (nowstr(), progname))
        sendEmail()
    elif (len(sys.argv) > 1) and (sys.argv[1] == '--help'):
        usage()
        exit(0)
    elif (len(sys.argv) > 1) and (sys.argv[1] == '-h'):
        usage()
        exit(0)
    else:
        if verbose: 
            print("INFO(%s): %s running with dns: %s" % (nowstr(), progname, dns))

    test(router, modem, internet, dns, 4) # 4 retries means 5 tries (1 minute per try)

