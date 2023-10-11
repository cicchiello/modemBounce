#!/usr/bin/python

import requests
import time
import datetime

def test(triesLeft):
    url = "https://google.com"
    try: 
        response = requests.get(url)
 
        #print("Status Code", response.status_code)
        print("INFO(%s): Passed" % (datetime.datetime.now()))

    except:
        print("INFO(%s): Failed (%d); pausing for one minute..." % (datetime.datetime.now(), triesLeft))
        if triesLeft > 0:
            time.sleep(60)
            test(triesLeft-1)
        else:
            # all tries exhausted...   have to bounce the modem
            print("INFO(%s): All tries exhausted; turning off the modems..." % datetime.datetime.now())
            # .. insert call to turn off the modems
            time.sleep(30)
            print("INFO(%s): turning modems back on..." % datetime.datetime.now())
            # .. insert call to turn on the modems



if __name__ == "__main__":
    test(5)
