/***************************************************************************
                          slist.c  -  skip-list 
                             -------------------
    begin                : Sat Oct 24 2002
    copyright            : (C) 2002 by Ume� University, Sweden
    email                : roland@catalogix.se

   COPYING RESTRICTIONS APPLY.  See COPYRIGHT File in top level directory
   of this package for details.

 ***************************************************************************/


#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>


#include <struct.h>
#include <db0.h>
#include <func.h>
#include <wrappers.h>
#include <macros.h>


/* An implementation of a skip-list */
/*
 In skip lists extra links are used to skip nodes in a list while 
 searching

 [ ]----------->[ ]----------->[ ]
 [ ]------>[ ]->[ ]->[ ]------>[ ]
 [ ]->[ ]->[ ]->[ ]->[ ]->[ ]->[ ]

 The number of nodes skipped is variable, though at each level at least
 as much as the larges on the immediate lower level
 */

/************************************************************************/

int boundary_xcmp( boundary_t *b1p, boundary_t *b2p )
{
  int      v = 0 ;
  uint32_t a, b ;

  /* NULL is a tail marker, everything is 'less' than the tail value */
  if( b2p == 0 ) return -1 ;

  /*
  traceLog("boundary_xcmp (%d)", b2p->type) ;
  boundary_print( b1p) ;
  boundary_print( b2p) ;
  */

  switch( b2p->type & 0x07 ) {
    case SPOC_ALPHA:
    case SPOC_DATE :

      if( b1p->v.val.len == 0 ) {
        if( b2p->v.val.len == 0 ) v = 0 ;
        else v = -1 ;
      }
      else if( b2p->v.val.len == 0 ) v = 1 ;
      else {
        if(( v = octcmp( &b1p->v.val, &b2p->v.val )) == 0 ) {
          v = b1p->type - b2p->type  ;
        }
      }
      break ;

    case SPOC_NUMERIC:
    case SPOC_TIME:
      if(( v = b1p->v.num - b2p->v.num) == 0 ) {
        v = b1p->type - b2p->type  ;
      }
      break ;

    case SPOC_IPV4:
      a = htonl(b1p->v.v4.s_addr) ;
      b = htonl(b2p->v.v4.s_addr) ;
      if( a == b ) v = (b1p->type&0xF0) - (b2p->type&0xF0) ;
      else if( a < b ) v = -1 ;
      else v = 1 ;
      break ;

    case SPOC_IPV6: /* Can be viewed as a array of unsigned ints  */
      v = ipv6cmp( &b1p->v.v6, &b2p->v.v6) ;

      if( !v ) v = b1p->type - b2p->type ;
      break ;
  }

  /* traceLog( "res: %d", v ) ; */

  return v ;
}

/************************************************************************/

slnode_t *sl_new( boundary_t *item, int n, slnode_t *tail )
{
  slnode_t *sln ;
  int       i ;
  
  sln = ( slnode_t * ) Malloc ( sizeof( slnode_t )) ;

  /* set the number of levels for this node */
  if( n ) {
    sln->next = ( slnode_t ** ) Calloc ( n, sizeof( slnode_t * )) ;
    sln->sz   = n ;

    for( i = 0 ; i < n ; i++ ) sln->next[i] = tail ;
  }
  else {
    sln->next = 0 ;
    sln->sz = 0 ;
  }

  sln->junc = 0 ;    /* joining the boundaries of a range */
  sln->item = item ; /* the data */
  sln->refc = 0 ;    /* reference counter to know if I should still keep it around */

  return sln ; 
}

/************************************************************************/

void sl_free_node( slnode_t *slp )
{
  int     i ;
  parr_t *pa ;

  if( slp ) {
    if( slp->item ) boundary_free( slp->item ) ;

    if( slp->junc ) {
      for( pa = slp->junc, i = 0 ; i < pa->n ; i++ )
        junc_free( (junc_t *) pa->vect[i] ) ;

      parr_free( slp->junc ) ;
    }

    if( slp->next ) free( slp->next ) ;

    free( slp ) ;
  }
}

/************************************************************************
 Initializing the list, starts of with only a head and a tail, representing
 the absolute minumum and maximum values in a range 
 ************************************************************************/

slist_t *sl_init( int max )
{
  slist_t *slp ;

  slp = ( slist_t * ) Malloc ( sizeof( slist_t )) ;

  slp->lgn = 0 ;       /* number of levels at this node */
  slp->lgnmax = max ;  /* max number of levels */
  slp->type = 0 ;      /* type of data: numeric, alpha, ... */

  slp->tail = sl_new( 0, 0, 0 ) ;
  slp->head = sl_new( 0, max+1, slp->tail ) ;

  return slp ;
}

/************************************************************************/

void sl_free_slist( slist_t *slp )
{
  slnode_t *np, **ne ;

  if( slp == 0 ) return ;

  np = slp->head ;

  while( np ) {
    ne = np->next ;
    sl_free_node( np ) ;
    if( ne == 0 ) break ; 
    np = ne[0] ;
  }
}

/************************************************************************/

/* Find the number of links */
/* the probability of getting a j-link node should be 1/(2**j) */
int sl_rand( slist_t *slp )
{
  int i, j, r = rand() ;
  int randmax = RAND_MAX, n ;
 
  if( slp == 0 ) return 0 ;

  for( i = 1, j = 2 ; i < slp->lgnmax ; i++, j += j ) {
    n = randmax >> j ;
    if( r > n ) break ;
  }

  if( i > slp->lgn ) slp->lgn = i ;
 
  return i ;
}

/************************************************************************/

void sl_rec_insert( slnode_t *old, slnode_t *new, int n )
{
  int r ;

  if(( r = boundary_xcmp( new->item, old->next[n]->item )) < 0 ) {
    if( n < new->sz ) {
      new->next[n] = old->next[n] ;
      old->next[n] = new ;
    }

    if( n == 0 ) return ;

    sl_rec_insert( old, new, n-1 ) ;

    return ;
  } 
  else
    sl_rec_insert( old->next[n], new, n ) ;
}

/************************************************************************/

slnode_t *sl_rec_delete( slnode_t *node, boundary_t *item, int n )
{
  slnode_t *next = node->next[n], *ret = 0 ;
  int       r ;

  if(( r = boundary_xcmp( next->item, item )) >= 0 ) { 
    if( r == 0 ) {
      if( !ret ) {
        next->refc-- ;
        ret = next ;
      }

      if( next->refc == 0 ) node->next[n] = next->next[n] ;
    }

    if( n == 0 ) return ret ;

    return sl_rec_delete( node, item, n-1 ) ;
  }

  return sl_rec_delete( next, item, n ) ;
}

/************************************************************************/

slnode_t *sl_delete( slist_t *slp, boundary_t *item )
{
  return  sl_rec_delete( slp->head, item, slp->lgn ) ;
}

/************************************************************************ 
 This is the actually search routine, starting at the top level and 
 working downwards.
 ************************************************************************/

slnode_t *sl_rec_find( slnode_t *node, boundary_t *item, int n, int *flag )
{
  if( node->sz == 0 ) return 0 ; /* end of list */

  if( boundary_xcmp( item, node->item ) == 0 ) {
    *flag = 0 ;
    return node ; /* The simple case */
  }

  if( boundary_xcmp( item, node->next[n]->item ) < 0 ) {

    if( n == 0 ) { /* reached the bottom */
      *flag = 1 ;
      return node->next[0] ; /* So what's return is the next bigger */
    } 

    return sl_rec_find( node, item, n-1, flag ) ;
  }
  else
    return sl_rec_find( node->next[n], item, n, flag ) ;

}

/************************************************************************/

slnode_t *sl_find( slist_t *slp, boundary_t *item )
{
  slnode_t *node ;
  int       flag = 0 ;

  node = sl_rec_find( slp->head, item, slp->lgn, &flag ) ;

  if( flag ) return 0 ;
  else       return node ;
}

/************************************************************************/

void boundary_print( boundary_t *bp ) 
{
  char ip[65], *lte = "-" ;

  if( bp == 0 ) return ;

  if( bp->type == 0 && bp->v.val.len == 0 ) return ;

  switch( bp->type & 0xF0 ) {
    case LT|GLE: 
     lte = "<=" ;
     break ;
    case GT|GLE: 
     lte = ">=" ;
     break ;
    case GLE: 
     lte = "=" ;
     break ;
    case GT: 
     lte = ">" ;
     break ;
    case LT: 
     lte = "<" ;
     break ;
  }
  switch( bp->type & 0x07 ) {
    case SPOC_ALPHA:
    case SPOC_DATE:
      if( bp->v.val.len ) traceLog(" %s %s", lte, bp->v.val.val ) ;
      else              traceLog(" NULL" ) ;
      break ;

    case SPOC_TIME:
    case SPOC_NUMERIC:
      traceLog(" %s %d", lte, bp->v.num ) ;
      break ;

    case SPOC_IPV4:
      inet_ntop( AF_INET, (void *) &bp->v.v4, ip, 65 ) ;
      traceLog(" %x %s %s", bp->type, lte, ip ) ;
      break ;

    case SPOC_IPV6:
      inet_ntop( AF_INET6, (void *) &bp->v.v6, ip, 65 ) ;
      traceLog(" %s %s", lte, ip ) ;
      break ;
  
  }
}

void sl_print( slist_t *slp )
{
  slnode_t *sn ;
  int       i ;

  if( slp == 0 ) return ;

  for( sn = slp->head, i = 0 ; sn->next ; sn = sn->next[0], i++ ) {
    traceLog( "%d:", i ) ;
    boundary_print( sn->item ) ; 
  }
}

/************************************************************************/

parr_t *sl_match( slist_t *slp, boundary_t *item ) 
{
  slnode_t *node, *nc ;
  int       flag = 0 ;
  parr_t   *lp = 0, *up = 0, *res ;

  if( slp == 0 ) {
    traceLog("Empty range ?" ) ;
    return 0 ;
  }

  DEBUG( SPOCP_DMATCH ) sl_print( slp ) ;
  node = sl_rec_find( slp->head, item, slp->lgn, &flag ) ;

  /* found a exakt match */
  if( flag == 0 ) {
    for( nc = slp->head ; nc != node->next[0] ; nc = nc->next[0] ) 
      lp = parr_join( lp, nc->junc ) ;

    if( node->item->type & GLE ) nc = node ;

    for( ; ; nc = nc->next[0] ) {
      up = parr_join( up, nc->junc ) ;
      if( nc->item == 0 ) break ;
    }
  }
  else {
    for( nc = slp->head ; nc != node ; nc = nc->next[0] ) 
      lp = parr_join( lp, nc->junc ) ;

    for( ; ; nc = nc->next[0] ) {
      up = parr_join( up, nc->junc ) ;
      if( nc->item == 0 ) break ;
    }
  }

  res = parr_or( lp, up ) ;

  parr_free( lp ) ; 
  parr_free( up ) ; 

  return res ;
}

/************************************************************************/

slnode_t *sl_insert( slist_t *slp, boundary_t *item ) 
{
  slnode_t *node ;

  /* is the value already in there ? */
  if(( node = sl_find( slp, item )) == 0 ) {

    /* If not create a new node and slip it in */
    node = sl_new( item, sl_rand( slp ), slp->tail ) ; 

    sl_rec_insert( slp->head, node, slp->lgn ) ;
  }
  
  node->refc++ ;

  return node ;
}

/************************************************************************/

parr_t *sl_range_match( slist_t *slp, range_t *rp )
{
  slnode_t *low, *high, *nc ;
  int       lflag = 0, hflag = 0 ;
  parr_t   *lp = 0, *up = 0, *res ;

  if(( rp->lower.type & 0xF0 ) == 0 ) { /* no value */
    low = slp->head ;
    /* lflag = 0 ; implicit */
  }
  else
    low = sl_rec_find( slp->head, &rp->lower, slp->lgn, &lflag ) ;

  if(( rp->upper.type & 0xF0 ) == 0 ) { /* no value */
    high = slp->tail ;
    /* hflag = 0 ;  implicit */
  }
  else
    high = sl_rec_find( slp->head, &rp->upper, slp->lgn, &hflag ) ;


  /* get every range the includes the present range */
  /* head --> low
                  high ---->tail */

  if( lflag == 0 ) {
    for( nc = slp->head ; nc != low->next[0] ; nc = nc->next[0] ) 
      lp = parr_xor( lp, nc->junc ) ;
  }
  else {
    for( nc = slp->head ; nc != low ; nc = nc->next[0] ) 
      lp = parr_xor( lp, nc->junc ) ;
  }

  for( nc = high ; ; nc = nc->next[0] ) {
    up = parr_xor( up, nc->junc ) ;
    if( nc->item == 0 ) break ;
  }

  res = parr_or( lp, up ) ;
  parr_free( lp ) ; 
  parr_free( up ) ; 

  return res ;
}

/************************************************************************/

junc_t *sl_range_add( slist_t *slp, range_t *rp ) 
{
  slnode_t *low, *upp ;
  junc_t   *jp ;

  if(( rp->lower.type & 0xF0 ) == 0 ) { /* no value */
    low = slp->head ;
    low->refc++ ;
  }
  else
    low = sl_insert( slp, &rp->lower ) ;

  if(( rp->upper.type & 0xF0 ) == 0 ) {
    upp = slp->tail ;
    upp->refc++ ;
  }
  else
    upp = sl_insert( slp, &rp->upper ) ;

  /* If there is no common junction add a new one */
  if( (jp = parr_common( low->junc, upp->junc)) == 0 ) {
    jp = junc_new() ;
    if( low->junc == 0 ) low->junc = parr_new( 2, &P_pcmp, 0, &P_pcpy, 0 ) ;
    low->junc = parr_add( low->junc, (void *) jp ) ;

    if( upp->junc == 0 ) upp->junc = parr_new( 2, &P_pcmp, 0, &P_pcpy, 0 ) ;
    upp->junc = parr_add( upp->junc, (void *) jp ) ;
  }

  return jp ;
}

/************************************************************************/

junc_t *sl_range_rm( slist_t *slp, range_t *rp, int *rc ) 
{
  junc_t   *jp = 0, *tmp ;
  slnode_t *low, *upp ;
  int       l,h,i ;

  if(( rp->lower.type & 0xF0 ) == 0 ) { /* no value */
    low = slp->head ;
    low->refc-- ;
  }
  else
    low = sl_delete( slp, &rp->lower ) ;


  if(( rp->upper.type & 0xF0 ) == 0 ) {
    upp = slp->tail ;
    upp->refc-- ;
  }
  else
    upp = sl_delete( slp, &rp->upper ) ;

  /* there should never be more than one in common */
  jp = ( junc_t * ) parr_common( low->junc, upp->junc ) ;

  l = parr_get_index( low->junc, jp ) ;
  h = parr_get_index( upp->junc, jp ) ;

  tmp = parr_get_item_by_index( low->junc, l ) ;
  parr_rm_index( &low->junc, l ) ;

  if( tmp && low->junc == 0 && low != slp->head ) sl_free_node( low ) ;

  tmp = parr_get_item_by_index( low->junc, h ) ;
  parr_rm_index( &upp->junc, h ) ;

  if( tmp && upp->junc == 0 && upp != slp->tail ) sl_free_node( upp ) ;

  /* if head is pointing to tail and both are down to 0 in ref count
     I could even remove the list, but why should I ? */

  for( i = 0 ; i < slp->lgnmax ; i++ )
    if( slp->head->next[i] != slp->tail ) break ; 

  if( i == slp->lgnmax && slp->head->refc == 0 ) *rc = 1 ;
  else *rc = 0 ;

  return jp ;
}

parr_t *get_all_range_followers( branch_t *bp, parr_t *pa )
{
  slnode_t  *node ;
  slist_t  **slp = bp->val.range ;
  int        i ;

  for( i = 0 ; i < DATATYPES ; i++ ) {
    if( slp[i] == 0 ) continue ;

    node = slp[i]->head ;
    if( node == 0 ) continue ;

    if( node->junc ) pa = parr_join( pa, node->junc ) ;

    do {
      node = node->next[0] ;
      if( node->junc ) pa = parr_join( pa, node->junc ) ;
    } while( node != slp[i]->tail ) ;
  }

  return pa ;
}

parr_t *sl_range_lte_match( slist_t *slp, range_t *rp, parr_t *pa )
{
  slnode_t *low, *high, *nc, *end ;
  int       lflag = 0, hflag = 0 ;
  parr_t   *res = 0 ;

  if(( rp->lower.type & 0xF0 ) == 0 ) { /* no value */
    low = slp->head ;
    /* lflag = 0 ; implicit */
  }
  else
    low = sl_rec_find( slp->head, &rp->lower, slp->lgn, &lflag ) ;

  if(( rp->upper.type & 0xF0 ) == 0 ) { /* no value */
    high = slp->tail ;
    /* hflag = 0 ;  implicit */
  }
  else
    high = sl_rec_find( slp->head, &rp->upper, slp->lgn, &hflag ) ;


  /* get every range that is included in the given range */

  nc = low ;

  if( hflag ) end = high ;
  else {
    if( high->next ) end = high->next[0] ; /* If there is one */
    else end = 0 ; /* have to break on other info */
  }

  for( nc = low ; nc != end ; nc = nc->next[0] ) {
    res = parr_join( res, nc->junc ) ;
    if( nc->next == 0 ) break ; /* reached the end node */
  }

  /* Remove every pointer with refcount < 2 */
  parr_dec_refcount( &res, 1 ) ;

  if( res ) {
    pa = parr_join( pa, res ) ;
    parr_free( res ) ;
  }

  return pa ;
}

boundary_t *boundary_dup( boundary_t *bp )
{
  boundary_t *nbp ;
  int         i ;
  char       *a,*b ;

  if( bp == 0 ) return 0 ;

  nbp = ( boundary_t * ) Malloc ( sizeof( boundary_t )) ;

  switch( bp->type & 0x07 ) {
    case SPOC_ALPHA:
    case SPOC_DATE :
      if( bp->v.val.len ) {
        nbp->v.val.val = Strndup( bp->v.val.val, bp->v.val.len ) ;
        nbp->v.val.len = bp->v.val.len ;
      }
      break ;

    case SPOC_NUMERIC:
    case SPOC_TIME:
      nbp->v.num = bp->v.num ;
      break ;

    case SPOC_IPV4:
      a = (char *) &nbp->v.v6 ;
      b = (char *) &bp->v.v6 ;
      for( i = 0 ; i < (int) sizeof( struct in_addr ) ; i++ ) *a++ = *b++ ;
      break ;

    case SPOC_IPV6:
      a = (char *) &nbp->v.v6 ;
      b = (char *) &bp->v.v6 ;
      for( i = 0 ; i < (int) sizeof( struct in6_addr ) ; i++ ) *a++ = *b++ ;
      break ;
  }

  nbp->type = bp->type ;

  return nbp ;
}

slnode_t *sln_dup( slnode_t *old )
{
  slnode_t *new ;
  
  if( old == 0 ) return 0 ;

  new = ( slnode_t * ) Malloc ( sizeof( slnode_t )) ;

  if( old->sz ) {
    new->next = ( slnode_t ** ) Calloc ( old->sz, sizeof( slnode_t * )) ;
    new->sz   = old->sz ;

    /* for( i = 0 ; i < old->sz ; i++ ) sln->next[i] = 0 ; */
  }
  else {
    new->next = 0 ;
    new->sz = 0 ;
  }

  new->sz = old->sz ;
  new->refc = old->refc ;
  new->item = boundary_dup( old->item ) ;

  return new ;
}

slist_t *sl_dup( slist_t *old, ruleinfo_t *ri )
{
  slist_t  *new = 0 ;
  slnode_t *slp, *nc = 0, *pc = 0, *oc, *bc ;
  int       i, j ;
  junc_t   *jp ;
  void     *vp ;

  if( old == 0 ) return 0 ;

  new = ( slist_t * ) Malloc ( sizeof( slist_t )) ;

  new->lgn = old->lgn ;        /* number of levels at this node */
  new->lgnmax = old->lgnmax ;  /* max number of levels */
  new->type = old->type ;      /* type of data: numeric, alpha, ... */
  new->n = old->n ;

  /* first all the nodes up until the tail */
  for ( oc = old->head ; oc->next != 0  ; oc = oc->next[0] ) {
    if( nc ) {
      nc->next[0] = sln_dup( oc ) ;
      nc = nc->next[0] ;
    }
    else {
      nc = sln_dup( oc ) ; 
      new->head = nc ;
    }
  } 

  /* and the tail */
  nc->next[0] = sln_dup( oc ) ;
  new->tail = nc->next[0] ;

  /* then connect them on all levels */

  /* first the default, the tail */
  for( nc = new->head ; nc->next ; nc = nc->next[0] ) {
    for( i = 1 ; i < nc->sz ; i++ ) nc->next[i] = new->tail ;
  }

  for( i = 1  ; i < old->lgnmax ; i++ ) {
    nc = new->head ; 
    oc = old->head ;

    while( oc != old->tail ) {
      pc = oc->next[i] ; 
      for( slp = oc->next[0], j = 1 ; slp != pc ; slp = slp->next[0] ) j++ ;
      oc = pc ;
      for( bc = nc ; j > 0 ; j--, bc = bc->next[0] ) ; 
      nc->next[i] = bc ;
      nc = bc ;
    }
  }

  /* and then the junctions, this is time consuming */

  for( oc = old->head, nc = new->head ; oc->next ; nc = nc->next[0], oc = oc->next[0] ) {
    for( bc = nc->next[0], pc = oc->next[0] ; pc->next ; pc = pc->next[0], bc = bc->next[0] ) {
      /* there should never be more than one common */
      if((vp = parr_common( oc->junc, pc->junc)) != 0 ) {

        jp = junc_dup((junc_t *) vp, ri ) ;

        if( nc->junc == 0 ) nc->junc = parr_new( 2, &P_pcmp, 0, &P_pcpy, 0 ) ;
        nc->junc = parr_add( nc->junc, (void *) jp ) ;

       if( bc->junc == 0 ) bc->junc = parr_new( 2, &P_pcmp, 0, &P_pcpy, 0 ) ;
        bc->junc = parr_add( bc->junc, (void *) jp ) ;
      }
    }
    /* there should never be more than one common */
    if((vp = parr_common( oc->junc, pc->junc)) != 0 ) {

      jp = junc_dup((junc_t *) vp, ri ) ;

      if( nc->junc == 0 ) nc->junc = parr_new( 2, &P_pcmp, 0, &P_pcpy, 0 ) ;
      nc->junc = parr_add( nc->junc, (void *) jp ) ;

      if( bc->junc == 0 ) bc->junc = parr_new( 2, &P_pcmp, 0, &P_pcpy, 0 ) ;
      bc->junc = parr_add( bc->junc, (void *) jp ) ;
    }
  }

  return new ;
}
