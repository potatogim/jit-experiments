#ifndef PJ_OPTREE_H_
#define PJ_OPTREE_H_

#include "EXTERN.h"
#include "perl.h"

/* Code relating to traversing and manipulating the OP tree */

namespace PerlJIT {
  class OPTreeWalker {
  public:
    enum walk_control_t {WALK_CONT = 0, WALK_SKIP = 1, WALK_ABORT = 2};

    /* Walks the OP tree left-hugging, depth-first, invoking the callback
     * for each op. Besides the regular WALK_CONT, the callback may return
     * WALK_SKIP to avoid recursing into the OP's children or WALK_ABORT
     * to stop walking the tree altogether. */
    void walk(pTHX_ OP *o, OP *parentop);

    // To be implemented ins subclass
    virtual walk_control_t op_callback(pTHX_ OP *o, OP *parentop);
  };
}

/* Starting from root OP, traverse the tree to find candidate OP for JITing
 * and perform actual replacement if at all. */
/* This function will internally call pj_attempt_jit on candidates,
 * which will, in turn, call this function on subtrees that it cannot
 * JIT. */
void pj_find_jit_candidate(pTHX_ OP *o, OP *parentop);


#endif
