#include "Logger.h" // include first to ensure it's self-contained

#include <Arduino.h>

#include <WiFi101.h>
#include "HttpLogger.h"
#include <stddef.h>

static const size_t LOG_QUEUE_DEPTH = 128;
static const size_t LOG_LINE_MAX = 90;

static const char *CLIP_STR = "...\n";
static const int CLIP_LEN = strlen(CLIP_STR)+2;


static void _LogAssertFunc(const char *msg, const char *file, int line)
{
  Serial.printf("ASSERT(%ld): msg %s\n", millis(), msg);
  Serial.printf("ASSERT(%ld): file %s\n", millis(), file);
  Serial.printf("ASSERT(%ld): line %d\n", millis(), line);
  delay(10);
  while (true) {delay(1);}
}

#define LogAssert(test,msg,file,linenum) {if (!test) {_LogAssertFunc(msg, file, linenum);}}

static char _queue[LOG_QUEUE_DEPTH][LOG_LINE_MAX];
static size_t _head = 0;
static size_t _tail = 0;
static size_t _count = 0;
static size_t _dropped = 0;


//
// Helper class to help debug missues of sharing of a scratchpad char buffer
// (When everything's coded properly, it'll be quite)
//
class SharedScratchBuf {
public:
  SharedScratchBuf(const char *file, int linenum)
    : mFile(file), mLinenum(linenum)
  {
    LogAssert(!sBufInUse,"SharedScratchBuf CTOR", file, linenum);
    sBufInUse = true;
  }
  ~SharedScratchBuf() {
    LogAssert(sBufInUse, "SharedScratchBuf DTOR", mFile, mLinenum);
    sBufInUse = false;
  }

  size_t len() const {return LOG_LINE_MAX;}
  static bool isInUse() {return sBufInUse;}
  char *c_str() {return sBuf;}

private:
  const char *mFile;
  const int mLinenum;
  static bool sBufInUse;
  static char sBuf[LOG_LINE_MAX];
};

/* static */
char SharedScratchBuf::sBuf[LOG_LINE_MAX];
/* static */
bool SharedScratchBuf::sBufInUse = false;



static void popOldest() {
  if (_count == 0) {
    return;
  }
  
  _head = (_head + 1) % LOG_QUEUE_DEPTH;
  _count--;
}


static void enqueue(const char *line) {
  LogAssert(strlen(line) > LOG_LINE_MAX - CLIP_LEN, "log line too long...", __FILE__, __LINE__);
  
  if (_count == LOG_QUEUE_DEPTH) {
    popOldest();
    _dropped++;
  }

  strncpy(_queue[_tail], line, LOG_LINE_MAX - 1);
  _queue[_tail][LOG_LINE_MAX - 1] = '\0';
  
  _tail = (_tail + 1) % LOG_QUEUE_DEPTH;
  _count++;
}


static void logHttpOrQueue(Logger *logger, HttpLogger *httpLogger, const char *line) {
  if (!httpLogger) {
    enqueue(line);
    return;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    enqueue(line);
    return;
  }

  LogAssert(!SharedScratchBuf::isInUse(), "logHttpOrQueue", __FILE__, __LINE__);
  logger->flush();
  
  // If flush did not empty the queue, HTTP is failing.
  // Queue the new message instead of trying to send it out of order.
  if (_count > 0) {
    enqueue(line);
    return;
  }
  
  if (!httpLogger->println(line)) {
    enqueue(line);
  }
}


void Logger::println(const char *line) {
  LogAssert(!SharedScratchBuf::isInUse(), "Logger::println", __FILE__, __LINE__);
  SharedScratchBuf buf(__FILE__, __LINE__);
  
  LogAssert(line[strlen(line)-1] != '\n', "line is CR-terminated", __FILE__, __LINE__);
    
  if (strlen(line) > LOG_LINE_MAX - CLIP_LEN) {
    Serial.println("log line too long...");
    strncpy(buf.c_str(), line, LOG_LINE_MAX - CLIP_LEN);
    buf.c_str()[LOG_LINE_MAX - CLIP_LEN] = '\0';
    strcat(buf.c_str(), CLIP_STR);
    if (_mode == LOG_TO_HTTP || _mode == LOG_TO_BOTH) {
      strcat(buf.c_str(), "\n");
    }
  } else {
    strcpy(buf.c_str(), line);
  }

  if (_mode == LOG_TO_SERIAL || _mode == LOG_TO_BOTH) {
    Serial.println(buf.c_str());
  }
  
  if (_mode == LOG_TO_HTTP || _mode == LOG_TO_BOTH) {
    logHttpOrQueue(this, _httpLogger, buf.c_str());
  }
}


void Logger::printf(const char *fmt, ...) {
  char line[LOG_LINE_MAX];  // needs its own buffer
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  
  LogAssert(line[strlen(line)-1] == '\n', "line isn't CR-terminated", __FILE__, __LINE__);
  
  if (strlen(line) > LOG_LINE_MAX - CLIP_LEN) {
    LogAssert(!SharedScratchBuf::isInUse(), "Logger::println", __FILE__, __LINE__);
    SharedScratchBuf buf(__FILE__, __LINE__);
    Serial.println("log line too long...");
    strncpy(buf.c_str(), line, LOG_LINE_MAX - CLIP_LEN);
    buf.c_str()[LOG_LINE_MAX - CLIP_LEN] = '\0';
    strcat(buf.c_str(), CLIP_STR);
    strcat(buf.c_str(), "\n");
    strcpy(line, buf.c_str());
  }

  LogAssert(line[strlen(line)-1] == '\n', "line isn't CR-terminated", __FILE__, __LINE__);
  
  if (_mode == LOG_TO_SERIAL || _mode == LOG_TO_BOTH) {
    // Serial.printf's buffer for formatting is small, so let's force use of println
    line[strlen(line)-1] = 0;  // I already know it's '\n', so terminate properly for println
    Serial.println(line);
    line[strlen(line)] = '\n'; // put back the '\n'
    line[strlen(line)] = 0;    // just in case
  }
  
  if (_mode == LOG_TO_HTTP || _mode == LOG_TO_BOTH) {
    LogAssert(!SharedScratchBuf::isInUse(), "Logger::printf", __FILE__, __LINE__);
    logHttpOrQueue(this, _httpLogger, line);
  }
}


void Logger::flush() {
  if (!_httpLogger) {
    return;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (_dropped > 0) {
    // indicate dropped messages with a synthesized log entry
    SharedScratchBuf buf(__FILE__, __LINE__);
    sprintf(buf.c_str(), "(delayed) WARNING(%ld): Logger dropped %d older messages\n", millis(), _dropped);
    
    if (!_httpLogger->printf(buf.c_str())) {
      return;
    }

    _dropped = 0;
  }

  while (_count > 0) {
    SharedScratchBuf buf(__FILE__, __LINE__);
    sprintf(buf.c_str(), "(delayed) %s", _queue[_head]);
    if (!_httpLogger->printf(buf.c_str())) {
      return;
    }
    
    popOldest();
  }
}



