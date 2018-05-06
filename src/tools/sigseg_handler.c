//
//  sigseg_handler.c
//
//  Created by Stuart Crook on 07/03/2018.
//

#include "sigseg_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <signal.h>
#include <execinfo.h>
#include <stdio.h>
#include <dlfcn.h>

static void signal_handler(int signal, siginfo_t *info, void *uap) {
    
    // fetch the return address from the thread state
    // darwin_mcontext64 ->
    //    __es exception state _STRUCT_X86_EXCEPTION_STATE64
    //    __ss thread state    _STRUCT_X86_THREAD_STATE64
    void *pc = (unsigned char *)((ucontext_t *)uap)->uc_mcontext->__ss.__rip;
    
    // find the symbol at that address
    Dl_info dl_info;
    int rc = dladdr(pc, &dl_info);
    if (rc) {
        printf("Signal %d @ %s() + %ld\n", signal, dl_info.dli_sname, (pc - dl_info.dli_saddr));
    } else {
        printf("dladdr() failed\n");
    }
    
    exit(EXIT_FAILURE);
}

void install_signal_handler(int signal) {
    struct sigaction action = { 0 };
    action.sa_sigaction = &signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(signal, &action, NULL);
}
