#ifndef PJ_DEBUG_H_
#define PJ_DEBUG_H_

#ifdef NDEBUG
#  define PJ_DEBUG (void)
#  define PJ_DEBUG_1 (void)
#  define PJ_DEBUG_2 (void)
#else
#  define PJ_DEBUGGING
#  define PJ_DEBUG(s) printf(s)
#  define PJ_DEBUG_1(s, par1) printf(s, par1)
#  define PJ_DEBUG_2(s, par1, par2) printf(s, par1, par2)
#endif

#endif
