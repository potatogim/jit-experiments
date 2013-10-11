#include "pj_keyword_plugin.h"
#include "pj_global_state.h"
#include "types.h"
#include "OPTreeVisitor.h"

#include <iostream>
#include <string>
#include <vector>
#include <tr1/unordered_map>

#include <EXTERN.h>
#include <perl.h>

using namespace PerlJIT;
using namespace std;
using namespace std::tr1;

namespace PerlJIT {
  // Walks OP tree and builds list of PADSV OPs that have the OPpLVAL_INTRO
  // flag set.
  class PadSvDeclarationOpExtractor: public OPTreeVisitor {
  public:
    OPTreeVisitor::visit_control_t visit_op(pTHX_ OP *o, OP *parentop);

    const vector<OP *> & get_padsv_ops()
      { return fPadSVOPs; }

  private:
    vector<OP *> fPadSVOPs;
  };
}


OPTreeVisitor::visit_control_t
PadSvDeclarationOpExtractor::visit_op(pTHX_ OP *o, OP *parentop)
{
  // this allows all kind of things, like applying an Array/Hash type
  // to a plain scalar; correctness is checked in pj_optree.c
  if ((o->op_type == OP_PADSV ||
       o->op_type == OP_PADAV ||
       o->op_type == OP_PADHV) && o->op_private & OPpLVAL_INTRO)
    fPadSVOPs.push_back(o);
  return VISIT_CONT;
}


// MAGIC hook to free the declaration/type data associated with a CV.
STATIC int
pj_type_annotate_mg_free(pTHX_ SV *sv, MAGIC* mg)
{
  PERL_UNUSED_ARG(sv);
  pj_declaration_map_t *decl_map = (pj_declaration_map_t*)mg->mg_ptr;
  delete decl_map;
  mg->mg_ptr = NULL;
  return 0;
}


// vtable for our CV-annotation for typed declarations
STATIC MGVTBL PJ_type_annotate_mg_vtbl = {
  0, 0, 0, 0, pj_type_annotate_mg_free, 0, 0, 0
};


// Returns NULL if the CV doesn't have any typed declarations.
pj_declaration_map_t *
pj_get_typed_variable_declarations(pTHX_ CV *cv)
{
  MAGIC *mg = mg_findext((SV *)cv, PERL_MAGIC_ext, &PJ_type_annotate_mg_vtbl);
  if (mg != NULL) {
    return (pj_declaration_map_t *)mg->mg_ptr;
  }
  else {
    return NULL;
  }
}


// Just advance the lexer until whitespace is found and return the
// string up to that point as a C++ string.
// Will stop at maxlen (and return "") unless maxlen is 0
STATIC string
S_lex_to_whitespace(pTHX, STRLEN maxlen)
{
  char *start = PL_parser->bufptr;
  char *s = PL_parser->bufptr;
  while (1) {
    char * const end = PL_parser->bufend;
    while (end-s >= 1) {
      if (isSPACE(*s)) {
        lex_read_to(s); /* skip Perl's lexer/parser ahead to end of type */
        return string(start, (size_t)(s-start));
      }
      else if (maxlen && (size_t)(s-start) > maxlen) {
        lex_read_to(s); /* skip Perl's lexer/parser ahead to end of type */
        lex_read_space(0);
        return string(""); /* end of code */
      }
      s++;
    }
    if ( !lex_next_chunk(LEX_KEEP_PREVIOUS) ) {
      lex_read_to(s); /* skip Perl's lexer/parser ahead to end of type */
      lex_read_space(0);
      return string(""); /* end of code */
    }
    else {
      // protect against realloc in lex_next_chunk
      const ptrdiff_t d = s-start;
      start = PL_parser->bufptr;
      s = start + d;
    }
  }
  abort(); // should never happen.
  return string("");
}

// Same as S_lex_to_whitespace, but doesn't commit. Doesn't read space after.
STATIC string
S_peek_to_whitespace(pTHX, STRLEN maxlen, char **endptr)
{
  char *start = PL_parser->bufptr;
  char *s = PL_parser->bufptr;
  while (1) {
    char * const end = PL_parser->bufend;
    while (end-s >= 1) {
      if (isSPACE(*s)) {
        if (endptr != NULL)
          *endptr = s;
        return string(start, (size_t)(s-start));
      }
      else if (maxlen && (size_t)(s-start) > maxlen)
        return string("");
      s++;
    }
    if ( !lex_next_chunk(LEX_KEEP_PREVIOUS) )
      return string("");
    else {
      // protect against realloc in lex_next_chunk
      const ptrdiff_t d = s-start;
      start = PL_parser->bufptr;
      s = start + d;
    }
  }
  abort(); // should never happen.
  return string("");
}

// Parse a Perl::JIT type. Croaks on error.
STATIC AST::Type *
S_parse_type(pTHX)
{
  I32 c;

  c = lex_peek_unichar(0);
  if (c < 0 || isSPACE(c))
    croak("syntax error");
  // inch our way forward to end-of-type
  string type_str =  S_lex_to_whitespace(aTHX, 0);
  if (type_str == string(""))
    croak("syntax error while extracting variable type");

  AST::Type *type = AST::parse_type(type_str);
  if (type == NULL)
    croak("syntax error '%s' does not name a type", type_str.c_str());
  return type;
}

// Fetches the declaration map from MAGIC attached to the CV or
// creates it if there's none yet.
STATIC pj_declaration_map_t *
S_get_or_makedeclaration_map(pTHX)
{
  pj_declaration_map_t *decl_map;

  decl_map = pj_get_typed_variable_declarations(aTHX_ PL_compcv);
  if (decl_map == NULL) {
    decl_map = new pj_declaration_map_t;
    (void)sv_magicext(
      (SV *)PL_compcv, NULL, PERL_MAGIC_ext, &PJ_type_annotate_mg_vtbl,
      (char *)decl_map, 0
    );
  }

  return decl_map;
}

// This handles the following cases, but doesn't include
// scanning over the "typed" keyword.
// typed <type> SCALAR
// typed <type> (DECL-LIST)
STATIC void
S_parse_typed_declaration(pTHX_ OP **op_ptr)
{
  I32 c;

  // Need space before type
  c = lex_peek_unichar(0);
  if (c < 0 || !isSPACE(c))
    croak("syntax error");
  lex_read_space(0);

  AST::Type *type = S_parse_type(aTHX);

  // Skip space (which we know to exist from S_lex_to_whitespace in S_parse_type)
  lex_read_space(0);

  // Oh man, this is so wrong. Secretly inject a bit of code into the
  // parse so that we can use Perl's parser to grok the declaration.
  lex_stuff_pvs(" my ", 0);

  // Let Perl parse the rest of the statement.
  OP *parsed_optree = parse_fullexpr(0);
  if (parsed_optree == NULL)
    croak("syntax error while parsing typed declaration");

  // Get the actual declaration OPs
  PadSvDeclarationOpExtractor extractor;
  extractor.visit(aTHX_ parsed_optree, NULL);
  const vector<OP *> &declaration_ops = extractor.get_padsv_ops();

  // Get existing declarations or create new container
  pj_declaration_map_t *decl_map = S_get_or_makedeclaration_map(aTHX);

  // Add to the declaration map
  const unsigned int ndecl = declaration_ops.size();
  for (unsigned int i = 0; i < ndecl; ++i) {
    (*decl_map)[declaration_ops[i]->op_targ] = TypedPadSvOp(type, declaration_ops[i]);
  }

  // Delete any unowned type or set ownership [FIXME this is terrible]
  if (decl_map->size() == 0)
    delete type;
  else
    (*decl_map)[declaration_ops.back()->op_targ].set_type_ownership(true);

  // Actually output the OP tree that Perl constructed for us.
  *op_ptr = parsed_optree;
}

STATIC void
S_parse_typed_loop(pTHX_ OP **op_ptr)
{
  I32 c;

  AST::Type *type = S_parse_type(aTHX);

  // Skip space (which we know to exist from S_lex_to_whitespace in S_parse_type)
  lex_read_space(0);

  // Oh man, this is so wrong. Secretly inject a bit of code into the
  // parse so that we can use Perl's parser to grok the declaration.
  lex_stuff_pvs(" for my ", 0);

  // Let Perl parse the full for(each) loop with the artificial header
  // from here on out:
  OP *parsed_optree = parse_fullstmt(0);
  if (parsed_optree == NULL)
    croak("syntax error while parsing typed loop variable declaration in for(each) loop");

  // Logic to find declaration
  assert(parsed_optree->op_type == OP_LINESEQ);
  assert(cUNOPx(parsed_optree)->op_first->op_type == OP_NEXTSTATE);
  OP *o = cUNOPx(parsed_optree)->op_first;
  assert(o->op_sibling->op_type == OP_LEAVELOOP);
  assert(o->op_sibling->op_first->op_type == OP_ENTERITER);
  o = cUNOPx(o->op_sibling)->op_first; // o is enteriter now

  // See pp_enteriter
  // This OP_ENTERITER is going to make do as a variable declaration.
  assert(o->op_targ); // "my" variable
  assert(o->op_private & OPpLVAL_INTRO); // for my $x (...)

  // Get existing declarations or create new container
  pj_declaration_map_t *decl_map = S_get_or_makedeclaration_map(aTHX);

  // Add to the declaration map
  (*decl_map)[o->op_targ] = TypedPadSvOp(type, o);
  (*decl_map)[o->op_targ].set_type_ownership(true);

  // Actually output the OP tree that Perl constructed for us.
  *op_ptr = parsed_optree;
}

STATIC int
S_parse_typed_keyword(pTHX)
{
  I32 c;

  lex_read_space(0);
  c = lex_peek_unichar(0);
  // Read more into buffer if necessary
  if (c < 0) {
    if ( !lex_next_chunk(LEX_KEEP_PREVIOUS) )
      return 0;
    else
      c = lex_peek_unichar(0);
  }
  if (c < 0 || isSPACE(c))
    return 0;

  char *end = NULL;
  string typed_kw_str = S_peek_to_whitespace(aTHX, 5, &end);

  if (typed_kw_str != string("typed")) {
    return 0;
  }
  lex_read_to(end); // commit

  return 1;
}


// Look at input, see if it starts with spaces followed by a valid identifier
// start character. Returns ptr to first character of identifier or NULL.
char *
S_peek_starts_like_identifier(pTHX)
{
  char *start = PL_parser->bufptr;
  char *s = PL_parser->bufptr;
  while (1) {
    char * const end = PL_parser->bufend;
    while (end-s >= 1) {
      if (!isSPACE(*s)) {
        return isWORDCHAR(*s) ? s : NULL;
      }
      s++;
    }
    if ( !lex_next_chunk(LEX_KEEP_PREVIOUS) ) {
      return 0;
    }
    else {
      // protect against realloc in lex_next_chunk
      const ptrdiff_t d = s-start;
      start = PL_parser->bufptr;
      s = start + d;
    }
  }
  abort(); // should never happen.
  return 0;
}

// Main keyword plugin hook for JIT type annotations
int
pj_jit_type_keyword_plugin(pTHX_ char *keyword_ptr, STRLEN keyword_len, OP **op_ptr)
{
  // Enforce lexical scope of this keyword plugin
  HV *hints;
  if (!(hints = GvHV(PL_hintgv)))
    return FALSE;
  if (!(hv_fetchs(hints, PJ_KEYWORD_PLUGIN_HINT, 0)))
    return FALSE;

  int ret;
  // Match "typed Int $foo"
  if (keyword_len == 5 && memcmp(keyword_ptr, "typed", 5) == 0)
  {
    SAVETMPS;
    S_parse_typed_declaration(aTHX_ op_ptr); // sets *op_ptr or croaks
    ret = KEYWORD_PLUGIN_EXPR;
    //ret = KEYWORD_PLUGIN_STMT;
    FREETMPS;
  }
  // Match "for typed Int $foo (1..10) {}"
  else if (   (keyword_len == 3 && memcmp(keyword_ptr, "for", 3) == 0)
           || (keyword_len == 7 && memcmp(keyword_ptr, "foreach", 7) == 0) )
  {
    int is_typed_loop = S_parse_typed_keyword(aTHX);

    if (!is_typed_loop) {
      // Try to see if it's the "for Int $foo (){}" form
      char *first_char = S_peek_starts_like_identifier(aTHX);
      if (first_char) {
        // Check whether we have to extend the buffer
        if (PL_parser->bufend - first_char < 3) {
          char *tmp = PL_parser->bufptr;
          if ( !lex_next_chunk(LEX_KEEP_PREVIOUS) )
            croak("syntax error");
          first_char = PL_parser->bufptr + (first_char-tmp);
        }

        // Check for "for my $foo"
        if (*first_char != 'm'
            || *(first_char+1) != 'y'
            || isWORDCHAR(*(first_char+2)))
        {
          is_typed_loop = 1;
        }
      }
    }

    if (is_typed_loop) {
      SAVETMPS;

      lex_read_space(0);
      S_parse_typed_loop(aTHX_ op_ptr); // sets *op_ptr or croaks
      ret = KEYWORD_PLUGIN_EXPR;
      FREETMPS;
    }
    else { // Normal for/forach, we don't handle that.
      ret = (*PJ_next_keyword_plugin)(aTHX_ keyword_ptr, keyword_len, op_ptr);
    }
  }
  else {
    ret = (*PJ_next_keyword_plugin)(aTHX_ keyword_ptr, keyword_len, op_ptr);
  }

  return ret;
}

