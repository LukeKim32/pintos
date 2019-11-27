#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);


void parseInput(char *file_name, char *arguments[]);
void BuildStackWith(char *arguments[], void **esp);

void findChildAndListenFinish(struct thread *eachThread, void *aux);
bool checkThreadFinishedByStatus(int status);

#endif /* userprog/process.h */