#ifndef _LOGGER_H
#define _LOGGER_H


#define LOG_DEBUG 0
#define LOG_INFO 1
#define LOG_WARN 2
#define LOG_ERROR 3

//#define LOGLEVEL LOG_DEBUG
#define LOGLEVEL LOG_INFO
//#define LOGLEVEL LOG_WARN

#define logger(level, ...) \
{ \
  if(level >= LOGLEVEL) \
    log_print(level, __FILE__, __LINE__, __VA_ARGS__ ); \
}

int log_init(char *logfile);
void log_print(int level, char* filename, int line, char *fmt,...);

#endif // for #ifndef _LOGGER_H
