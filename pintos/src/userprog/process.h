#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void BuildStackWith(char *arguments[], void **esp);

/** @Deprecated */
void parseInput(char *file_name, char *arguments[]);
/** @Deprecated */
void findChildAndListenFinish(struct thread *eachThread, void *aux);
/** @Deprecated */
bool checkThreadFinishedByStatus(int status);
/** @Deprecated */
int getExitStatusOfChildBy(tid_t child_tid, struct thread * thread);

#endif /* userprog/process.h */
