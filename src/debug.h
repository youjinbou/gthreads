#ifndef _DEBUG_H_
#define _DEBUG_H_

#ifdef DEBUG

#define DBG(a...) fprintf(stderr,a)
#define DBG_FUN(a) DBG("%s : %s\n", __func__, a)

#else

#define DBG(a...) 
#define DBG_FUN(a)

#endif

#endif
