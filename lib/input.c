/***************************************************************************
                             input.c  

   -  routines to parse canonical S-expression into the incore representation

                             -------------------

    begin                : Sat Oct 12 2002
    copyright            : (C) 2002 by Ume� University, Sweden
    email                : roland@catalogix.se

   COPYING RESTRICTIONS APPLY.  See COPYRIGHT File in top level directory
   of this package for details.

 ***************************************************************************/

#define _XOPEN_SOURCE

#include <struct.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <time.h>

#include <config.h>

#include <macros.h>
#include <wrappers.h>
#include <func.h>
#include <spocp.h>
#include <proto.h>


spocp_result_t  get_str( octet_t *so, octet_t *ro ) ;
spocp_result_t  get_and( octet_t *oct, element_t *ep ) ;

atom_t         *get_atom( octet_t *op, spocp_result_t *rc ) ;
atom_t         *atom_new( octet_t *op ) ;

spocp_result_t  do_prefix( octet_t *oct, element_t *ep ) ;
spocp_result_t  do_suffix( octet_t *oct, element_t *ep ) ;
spocp_result_t  do_or( octet_t *oct, element_t *ep ) ;
spocp_result_t  do_range( octet_t *oct, element_t *ep ) ;
spocp_result_t  do_list( octet_t *tag, octet_t *list, element_t *ep, char *base ) ;
 
spocp_result_t  is_valid_range( range_t *rp ) ;
spocp_result_t  is_atom( range_t *rp ) ;

spocp_result_t  set_limit( boundary_t *bp, octet_t *op ) ;
boundary_t     *set_delimiter( range_t *range, octet_t oct ) ;
char           *print_len_spec( char *s, int n ) ;

/* =============================================================== */

element_t *element_new()
{
  element_t *ep ;

  ep = (element_t *) Calloc (1, sizeof( element_t )) ;

  return ep ;
}


int get_len( octet_t *op )
{
  char *ep, *sp = op->val ;
  long l ;

  /* strtol skips SP (0x20) and allows a leading '-'/'+' so
     I have to check that first, since those are not allowed in
     this context */

  if( *sp == ' ' || *sp == '-' || *sp == '+' ) return -1 ;

  l = strtol( sp, &ep, 10 ) ;

  /* no number read */
  if( ep == sp ) return -1 ;

  /* ep points to the first non digit character or '\0' */
  op->len -= ep - sp ;
  op->val = ep ;

  return (int) l  ;
}

spocp_result_t get_str( octet_t *so, octet_t *ro )
{
  ro->size = 0 ;

  ro->len = get_len( so ) ;

  if( ro->len < 0 ) {
    LOG(SPOCP_ERR) traceLog( "In string \"%s\" ", so->val ) ;
    LOG(SPOCP_ERR) traceLog( "parse error: error in lengthfield" ) ;
    return SPOCP_SYNTAXERROR ;
  }

  if(( so->len - 1) < ro->len) {
    LOG(SPOCP_ERR) {
      traceLog( "expected length of string (%d) exceeds input length (%d)",
                 ro->len, so->len ) ;
      traceLog( "Offending part \"%c%c%c\"", so->val[0], so->val[1], so->val[2] ) ;
    }
    return SPOCP_MISSING_CHAR ;
  }

  if( *so->val != ':' ) {
    LOG(SPOCP_ERR) traceLog( "parse error: missing \":\"" ) ;
    return SPOCP_SYNTAXERROR ;
  }

  so->val++ ;
  so->len-- ;

  if( ro->len == 0 ) {
    LOG(SPOCP_ERR) traceLog( "Zero length strings not allowed" ) ;
    return SPOCP_SYNTAXERROR ;
  }

  ro->val = so->val ;
  ro->size = 0 ;        /* signifying that it's not dynamically allocated */

  so->val += ro->len ;
  so->len -= ro->len ;

  DEBUG(SPOCP_DPARSE)
    traceLog( "Got 'string' \"%*.*s\"", ro->len, ro->len, ro->val ) ;

  return SPOCP_SUCCESS ;
}

atom_t *atom_new( octet_t *op )
{
  atom_t *ap ;

  ap = ( atom_t * ) Malloc ( sizeof( atom_t )) ;

  if( op ) {
    ap->val.val = Strndup( op->val, op->len ) ;
    ap->val.len = op->len ;

    ap->hash = lhash( (unsigned char *) op->val, op->len, 0 ) ;
  }
  else {
    ap->val.val = 0 ;
    ap->val.len = 0 ;
    ap->hash = 0 ;
  }

  return ap ;
}

atom_t *get_atom( octet_t *op, spocp_result_t *rc )
{
  octet_t  oct ;

  if(( *rc = get_str( op, &oct )) != SPOCP_SUCCESS) {
    return 0 ;
  }

  return atom_new( &oct ) ;
}

spocp_result_t do_prefix( octet_t *oct, element_t *ep )
{
  spocp_result_t rc = SPOCP_SUCCESS ;

  DEBUG(SPOCP_DPARSE) traceLog( "Parsing 'prefix' expression" ) ;

  ep->type = SPOC_PREFIX ;

  if(( ep->e.prefix = get_atom( oct, &rc )) == 0 ) return rc ;

  /* a check for external references, which is not allowed in ranges */

  if( *oct->val != ')' ) { 
    LOG(SPOCP_ERR) traceLog( "Missing ending ')'" ) ;
    return SPOCP_SYNTAXERROR;
  }
  else {
    oct->val++ ;
    oct->len-- ;
  }

  return SPOCP_SUCCESS ;
}

spocp_result_t do_suffix( octet_t *oct, element_t *ep )
{
  spocp_result_t rc = 0 ;

  DEBUG(SPOCP_DPARSE) traceLog( "Parsing 'suffix' expression" ) ;

  ep->type = SPOC_SUFFIX ;

  if(( ep->e.suffix = get_atom( oct, &rc )) == 0 ) return rc ;

  if( *oct->val != ')' ) { 
    LOG(SPOCP_ERR) traceLog( "Missing ending ')'" ) ;
    return SPOCP_SYNTAXERROR ;
  }
  else {
    oct->val++ ;
    oct->len-- ;
  }

  return SPOCP_SUCCESS ;
}

/* ----------------------------------- */

set_t *set_new( int size )
{
  set_t *a ;

  a = ( set_t * ) Calloc (1, sizeof( set_t )) ;

  if( size ) {
    a->size = size ;
    a->n = 0 ;
    a->element = ( element_t ** ) Calloc ( size, sizeof( element_t * )) ;
  }

  return a ;
}

/*
void set_free( set_t *a )
{
  int i ;

  if( a ) {
    if( a->size ) {
      for( i = 0 ; i < a->n ; i++ ) element_free( a->element[i] ) ;
      free( a->element ) ;
    }
    free( a ) ;
  }
}
*/

set_t *set_join( set_t *a, set_t *b )
{
  element_t **tmp ;
  int      i, n ;

  if( a == 0 ) {
    a = set_new( 0 ) ;
    a->n = b->n ;
    a->size = b->size ;
    a->element = b->element ;
    b->n = b->size = 0 ;
    b->element = 0 ;
  }
  else {
    if(( a->size - a->n ) < b->n ) { /* not enough space */
      n = a->size ;
      do {
        a->size += n ;
      } while(( a->size - a->n ) < b->n ) ;

      tmp = realloc( a->element, a->size * sizeof( element_t * )) ;

      a->element = tmp ;
    }

    for( i = 0 ; i < b->n ; i++ )
     a->element[a->n++] = b->element[i] ;
  }

  return a ;
}

set_t *set_add( set_t *a, element_t *ep )
{
  element_t **tmp ;

  if( a == 0 ) {
    a = set_new( 4 ) ;
  }

  if( a->n == a->size ) {
    a->size *= 2 ;
    tmp = realloc( a->element, a->size * sizeof( element_t * )) ;
    a->element = tmp ;
  }

  a->element[a->n++] = ep ; 

  return a ;
}

/* ----------------------------------- */

void set_memberof( set_t *s, element_t *group ) 
{
  int i ;

  for( i = 0 ; i < s->n ; i++ ) s->element[i]->memberof = group ;
}


/* ----------------------------------- */

static spocp_result_t get_set( octet_t *oct, element_t *ep )
{
  element_t      *nep = 0 ;
  set_t          *pa, *sa = 0 ;
  spocp_result_t rc ;

  /* until end of star expression is found */
  while( oct->len && *oct->val != ')' ) {

    if(( rc = element_get( oct, &nep )) != SPOCP_SUCCESS ) {
      return rc ;
    }

    /* what if nep is of the same type as ep, deflate */
    if( nep->type == ep->type ) {
      pa = nep->e.set ;

      sa = set_join( sa, pa ) ;
      set_free( pa, 0 ) ;
    }
    else {
      sa = set_add( sa, nep ) ;
    }
  }

  /* only one item in the and expr, so it's not really a set. It's just a
     element and should therefore be treated as such */
  if( sa->n == 1 ) {
    ep = sa->element[0] ;
    set_free( sa, 0 ) ;
  }
  else {
    ep->e.set = sa ;
  }

  set_memberof( sa, ep ) ;

  if( *oct->val == ')' ) {
    oct->val++ ;
    oct->len-- ;
  }

  return SPOCP_SUCCESS ;
}

/* -------------------------------------------------------------------------- */

spocp_result_t do_or( octet_t *oct, element_t *ep )
{
  DEBUG(SPOCP_DPARSE) traceLog( "Parsing 'or' expression" ) ;

  ep->type = SPOC_OR ; 

  return get_set( oct, ep ) ;
}

/* -------------------------------------------------------------------------- */

spocp_result_t do_bcond( octet_t *oct, element_t *ep ) 
{
  element_t      *nep = 0 ;
  set_t          *sa = 0 ;
  spocp_result_t rc ;

  while( oct->len && *oct->val != ')' ) {

    if(( rc = element_get( oct, &nep )) == SPOCP_SYNTAXERROR ) return rc ;

    if( nep->type != SPOC_ATOM ) {
      element_free( nep ) ;
      set_free( sa, 1 ) ;
      return SPOCP_SYNTAXERROR ;
    }

    /* the real test of it's a external reference comes later, sofar if it's a atom it's OK */

    nep->memberof = ep ;

    sa = set_add( sa, nep ) ;
  }

  ep->type = SPOC_BCOND ;
  ep->e.set = sa ;

  if( *oct->val == ')' ) {
    oct->val++ ;
    oct->len-- ;
  }

  return SPOCP_SUCCESS ;
}

/* -------------------------------------------------------------------------- */

boundary_t *set_delimiter( range_t *range, octet_t oct )
{
  boundary_t *bp = 0 ;

  if( oct.len != 2 ) return 0 ;

  switch( oct.val[0] ) {
    case 'g' :
      switch( oct.val[1] ) {
        case 'e' :
          range->lower.type |= GLE ;
          bp = &range->lower ;
          break ;
      
        case 't' :
          range->lower.type |= GT ;
          bp = &range->lower ;
          break ;
      }
      break ;
 
    case 'l' :
      switch( oct.val[1] ) {
        case 'e' :
          range->upper.type |= GLE ;
          bp = &range->upper ;
          break ;
 
        case 't' :
          range->upper.type |= LT ;
          bp = &range->upper ;
          break ;
      }
      break ;
  }

  return bp ;
}

/* -------------------------------------------------------------------------- */

void to_gmt( octet_t *s, octet_t *t )
{
  char      *sp, time[20] ;
  struct tm tm ;
  time_t    tid ;

  if( s->len == 19 || s->len == 20 ) {
    t->val = Strndup( s->val, 19 ) ;
    t->len = 19 ;
  }
  else { /* offset */
    strncpy( time, s->val, 19 ) ;
    time[19] = '\0' ;
    strptime(time, "%Y-%m-%dT%H:%M:%S", &tm);
    tid = mktime( &tm ) ;

    sp = s->val + 19 ;
    if( *sp == '+' ) {
      sp++ ;
      tid += ( *sp++ - '0' ) * 36000 ;
      tid += ( *sp++ - '0' ) * 3600 ;
      if( s->len > 22 ) {
        s++ ;
        tid += ( *sp++ - '0' ) * 600 ;
        tid += ( *sp++ - '0' ) * 60 ;
      }
    }
    else {
      sp++ ;
      tid -= ( *sp++ - '0' ) * 36000 ;
      tid -= ( *sp++ - '0' ) * 3600 ;
      if( s->len > 22 ) {
        s++ ;
        tid -= ( *sp++ - '0' ) * 600 ;
        tid -= ( *sp++ - '0' ) * 60 ;
      }
    }

    localtime_r( &tid, &tm ) ;

    t->val = ( char * ) Malloc( 20 * sizeof( char )) ;
    t->val[19] = 0 ;

    strftime(t->val, 20, "%Y-%m-%dT%H:%M:%S", &tm);
    t->len = 19 ;
  }
}

void hms2int( octet_t *op, long *num )
{
  char *cp ;

  cp = op->val ;

  *num += ( *cp++ - '0' ) * 36000 ;
  *num += ( *cp++ - '0' ) * 3600 ;
  
  cp++ ;
 
  *num += ( *cp++ - '0' ) * 600 ;
  *num += ( *cp++ - '0' ) * 60 ;
  
  cp++ ;
 
  *num += ( *cp++ - '0' ) * 10 ;
  *num += ( *cp++ - '0' ) ;
}

/* -------------------------------------------------------------------------- */

spocp_result_t set_limit( boundary_t *bp, octet_t *op )
{
  int r = SPOCP_SYNTAXERROR ;

  switch( bp->type & 0x07 ) {
    case SPOC_NUMERIC:
      r = is_numeric( op, &bp->v.num ) ;
      break ;

    case SPOC_ALPHA :
      bp->v.val.val = Strndup( op->val, op->len ) ;
      bp->v.val.len = op->len ;
      r = SPOCP_SUCCESS ;
      break ;

    case SPOC_DATE:
      if(( r = is_date( op )) == SPOCP_SUCCESS ) 
        to_gmt( op, &bp->v.val ) ;
      break ;

    case SPOC_TIME:
      if(( r = is_time( op )) == SPOCP_SUCCESS ) 
        hms2int( op, &bp->v.num ) ;
      break ;

    case SPOC_IPV4:
      r = is_ipv4( op, &bp->v.v4 ) ;
      break ;

    case SPOC_IPV6:
      r = is_ipv6( op, &bp->v.v6 ) ;
      break ;
  }

  return r ;
}

int ipv4cmp( struct in_addr *ia1, struct in_addr *ia2 )
{
  return ia2->s_addr - ia1->s_addr ;
}

int ipv6cmp( struct in6_addr *ia1, struct in6_addr *ia2 )
{
  uint32_t *lw = ( uint32_t *) ia1 ;
  uint32_t *uw = ( uint32_t *) ia2 ;
  int r,i ;

  for( i = 0 ; i < 4 ; i++, lw++, uw++ ) {
    r = *uw - *lw ;
    if( r ) return r ;
  }

  return  0 ;
}

/* what's invalid ?
   1) the upper boundary being lower than the lower
   2) not allowing the only possible value to be within the
      boundaries equivalent to ( x > 5 && x < 5 )

 Returns: FALSE if invalid
          TRUE if valid
 */
spocp_result_t is_valid_range( range_t *rp )
{
  spocp_result_t r = SPOCP_SUCCESS ;
  int            c = 0 ;

  if( rp->lower.type & 0xF0 && rp->upper.type & 0xF0 ) {

    switch( rp->lower.type & 0x07 ) {
      case SPOC_NUMERIC :
      case SPOC_TIME :
        c = rp->upper.v.num - rp->lower.v.num ;
        break ;

      case SPOC_IPV4 :
        c = ipv4cmp( &rp->lower.v.v4,  &rp->upper.v.v4 ) ;
        break ;

      case SPOC_IPV6 :
        c = ipv6cmp( &rp->lower.v.v6,  &rp->upper.v.v6 ) ;
        break ;

      case SPOC_ALPHA :
      case SPOC_DATE :
        c = octcmp( &rp->lower.v.val, &rp->upper.v.val ) ;
        break ;

      default:
        return SPOCP_UNKNOWN_TYPE ;
    }

    if( c < 0 ) {
      LOG(SPOCP_ERR) traceLog( "Upper limit less then lower") ;
      r = SPOCP_SYNTAXERROR ;
    }
    else if( c == 0 && !(rp->upper.type & GLE) && !(rp->lower.type & GLE)) {
      LOG(SPOCP_ERR) traceLog( "Upper limit equal to lower when it shouldn't") ;
      r = SPOCP_SYNTAXERROR ;
    }
  }

  return r ;
}

/* checks whether the range in fact is an atom */
spocp_result_t is_atom( range_t *rp )
{
  uint32_t *u,   *l ;
  spocp_result_t sr = SPOCP_SYNTAXERROR ;

  switch( rp->lower.type & 0x07 ) {
    case SPOC_NUMERIC :
    case SPOC_TIME :
      if( rp->lower.v.num == 0 ) ;
      else if( rp->lower.v.num == rp->upper.v.num ) sr = SPOCP_SUCCESS ;
      break ;

    case SPOC_IPV4 :
      l = ( uint32_t * ) &rp->lower.v.v4 ;
      u = ( uint32_t * ) &rp->upper.v.v4 ;
      if( *l == *u ) sr = SPOCP_SUCCESS ;
      break ;

    case SPOC_IPV6 :
      l = ( uint32_t * ) &rp->lower.v.v6 ;
      u = ( uint32_t * ) &rp->upper.v.v6 ;
      if( l[0] == u[0] && l[1] == u[1] && l[2] == u[2] && l[3] == u[3] ) sr = SPOCP_SUCCESS ;
      break ;

    case SPOC_ALPHA :
    case SPOC_DATE :
      if( rp->lower.v.val.len == 0 && rp->upper.v.val.len == 0 ) sr = SPOCP_SUCCESS ;

      if( rp->lower.v.val.len == 0 || rp->upper.v.val.len == 0 ) sr = SPOCP_SUCCESS ;
      else if( octcmp( &rp->lower.v.val , &rp->upper.v.val ) == 0 ) sr = SPOCP_SUCCESS ;
      break ;
  }

  return sr ;
}

spocp_result_t do_range( octet_t *op, element_t *ep )
{
  octet_t     oct ;
  range_t    *rp = 0 ;
  boundary_t *bp ;
  int         r = SPOCP_SUCCESS ;

  DEBUG(SPOCP_DPARSE) traceLog( "Parsing range" ) ;

  /* next part should be type specifier */

  if(( r = get_str( op, &oct )) != SPOCP_SUCCESS ) goto done ;

  ep->type = SPOC_RANGE ;
  DEBUG( SPOCP_DPARSE ) traceLog ( "new_range" ) ;
  rp = ( range_t *) Calloc ( 1, sizeof( range_t )) ;

  ep->e.range = rp ;

  switch( oct.len ) {
    case 4 :
      if( strncasecmp( oct.val, "date", 4 ) == 0 ) rp->lower.type = SPOC_DATE ;
      else if( strncasecmp( oct.val, "ipv4", 4 ) == 0 ) rp->lower.type = SPOC_IPV4 ;
      else if( strncasecmp( oct.val, "ipv6", 4 ) == 0 ) rp->lower.type = SPOC_IPV6 ;
      else if( strncasecmp( oct.val, "time", 4 ) == 0 ) rp->lower.type = SPOC_TIME ;
      else {
        LOG(SPOCP_ERR) traceLog( "Unknown range type" ) ;
        r = SPOCP_SYNTAXERROR ;
      }
      break ;

    case 5:
      if( strncasecmp( oct.val, "alpha", 5 ) == 0 ) rp->lower.type = SPOC_ALPHA ;
      else {
        LOG(SPOCP_ERR) traceLog( "Unknown range type" ) ;
        r = SPOCP_SYNTAXERROR ;
      }
      break ;

    case 7:
      if( strncasecmp( oct.val, "numeric", 7 ) == 0 ) rp->lower.type = SPOC_NUMERIC ;
      else {
        LOG(SPOCP_ERR) traceLog( "Unknown range type" ) ;
        r = SPOCP_SYNTAXERROR ;
      }
      break ;

    default:
      LOG(SPOCP_ERR) traceLog( "Unknown range type" ) ;
      r = SPOCP_SYNTAXERROR ;
      break ;
  }

  if( r == SPOCP_SYNTAXERROR ) goto cleanup ;

  rp->upper.type = rp->lower.type ;

  if( *op->val == ')' ) { /* no limits defined */
    rp->lower.v.num = 0 ;
    rp->upper.v.num = 0 ;
    op->val++ ;
    op->len-- ;
    goto done ;
  }

  /* next part should be limiter description 'lt'/'le'/'gt'/'ge' */

  if(( r = get_str( op, &oct )) != SPOCP_SUCCESS ) goto cleanup ;

  if(( bp = set_delimiter( rp, oct )) == 0 ) {
    LOG(SPOCP_ERR) traceLog( "Error in delimiter specification" ) ;
    r = SPOCP_SYNTAXERROR ;
    goto cleanup ;
  }

  /* and then some value */
  /* it can't be a external reference */

  if(( r = get_str( op, &oct )) != SPOCP_SUCCESS ) goto cleanup ;

  if(( r = set_limit( bp, &oct )) != SPOCP_SUCCESS ) goto cleanup ;

  if( r == SPOCP_SUCCESS && *op->val == ')' ) { /* no more limit defined */
    op->val++ ;
    op->len-- ;
    goto done ;
  }

  /* next part should be limiter description 'lt'/'le'/'gt'/'ge'
     can't/shouldn't (?) be the same as above
   */

  if(( r = get_str( op, &oct )) != SPOCP_SUCCESS ) goto cleanup ;

  if( r == SPOCP_SUCCESS && ( bp = set_delimiter( rp, oct )) == 0 ) {
    LOG(SPOCP_ERR) traceLog( "Error in delimiter specification" ) ;
    r = SPOCP_SYNTAXERROR ;
    goto cleanup ;
  }

  /* and then some value */

  if(( r = get_str( op, &oct )) != SPOCP_SUCCESS ) goto cleanup ;

  if(( r = set_limit( bp, &oct )) != SPOCP_SUCCESS ) goto cleanup;

  if( r == SPOCP_SUCCESS && *op->val != ')' ) {
    LOG(SPOCP_ERR) traceLog( "Missing closing ')'" ) ;
    r = SPOCP_SYNTAXERROR ;
    goto cleanup ;
  }

  if(( r = is_valid_range( rp )) != SPOCP_SUCCESS )  goto cleanup ;

  if( is_atom( rp ) == SPOCP_SUCCESS ) {
    ep->type = SPOC_ATOM ;
    ep->e.atom = atom_new( &oct) ;
    range_free( rp ) ;
  }

cleanup:
  if( r != SPOCP_SUCCESS ) {
    range_free( rp ) ;
    ep->e.range = 0 ;
  }
  else {
    /*
    boundary_print( &rp->lower ) ;
    boundary_print( &rp->upper ) ;
    */
    op->val++ ;
    op->len-- ;
  }

done:
  return r ;
}

spocp_result_t do_list( octet_t *to, octet_t *lo, element_t *ep, char *base )
{
  spocp_result_t r = SPOCP_SUCCESS ;
  list_t         *lp = 0 ;
  element_t      *nep = 0, *pep ;

  DEBUG(SPOCP_DPARSE) traceLog( "List tag" ) ;

  ep->type = SPOC_LIST ;
  lp = ( list_t *) Malloc ( sizeof( list_t )) ;
  ep->e.list = lp ;

  pep = lp->head = element_new() ;

  /* first element has to be a atom */
  pep->memberof = ep ;
  pep->type = SPOC_ATOM ;
  pep->e.atom = atom_new( to ) ;

  /* get rest of the element */

  while( lo->len ) {
    nep = 0 ;

    r = element_get( lo, &nep ) ;

    if( r != SPOCP_SUCCESS ) {
      list_free( lp ) ;
      ep->e.list = 0 ;
      return r ;
    }

    if( nep == 0 ) break ; /* end of rule */

    pep->next = nep ;
    nep->memberof = ep ;
    pep = nep ;
  }

  /* sanity check */
  if( nep != 0 ) return SPOCP_SYNTAXERROR ;

  DEBUG( SPOCP_DPARSE ) {
    char *tmp ;
    tmp = oct2strdup( to, '\\' ) ;
    traceLog( "Input end of list with tag [%s]", tmp ) ;
    free( tmp ) ;
  }
  /* compute hash of the whole list in string representation 
     This is sort of an artifact, has very limited usage
   */
  lp->hstrlist = lhash( (unsigned char *) base, lo->val - base, 0 ) ;

  return SPOCP_SUCCESS ;
}

/* parse a string until I've got one full query */
/*
   S-expression format is
   (3:tag7:element)
   the so called canonical representation, and not
   (tag element)

   which among other things means that spaces does not occur
   between elements
 */

spocp_result_t element_get( octet_t *op, element_t **epp )
{
  char           *base ;
  octet_t         oct ;
  spocp_result_t  r = SPOCP_SUCCESS ;
  element_t      *ep = 0 ;

  if( op->len == 0 ) return SPOCP_SYNTAXERROR ;

  /* Why ? Is this still needed ? */
  if( *op->val == ')' ) {
    op->val++ ;
    op->len-- ;
    *epp = 0 ;
    return SPOCP_SUCCESS ;
  }

  *epp = ep = element_new() ;

  if( *op->val == '(' ) {  /* start of list or set */
    base = op->val ;

    op->val++ ;
    op->len-- ;

    if(( r = get_str( op, &oct )) == SPOCP_SUCCESS ) {

      if( oct.len == 1 && oct.val[0] == '*' ) { /* not a simple list, star expression */

        if(( r = get_str( op, &oct )) == SPOCP_SUCCESS ) {

          /* unless proven otherwise */
          r = SPOCP_SYNTAXERROR ;
  
          switch( oct.len ) {
            case 6 :
              if( strncmp( oct.val, "prefix", 6 ) == 0 ) r = do_prefix( op, ep ) ;
              else if( strncmp( oct.val, "suffix",6 ) == 0 ) r = do_suffix( op, ep ) ;
              break ;
          
            case 5:
              if( strncmp( oct.val, "range", 5) == 0 ) r = do_range( op, ep ) ;
              else if( strncmp( oct.val, "bcond", 5) == 0 ) r = do_bcond( op, ep ) ;
              break ;
    
            case 2:
              if( strncmp( oct.val, "or", 2) == 0 ) r = do_or( op, ep ) ;
              break ;
          }
        }
      }
      else 
        r = do_list( &oct, op, ep, base ) ;
      
    }
  }
  else { /* has to be a atom thingy */
    ep->type = SPOC_ATOM ;
    if(( ep->e.atom = get_atom( op, &r )) == 0 ) {
      element_free( ep ) ;
      return r ;
    }
  }

  if( r != SPOCP_SUCCESS ) {
    element_free( ep ) ;
    *epp = 0 ;
  }

  return r ;
}

/* ============================================================================ */
/* ============================================================================ */
