
/***************************************************************************
			  db0.c  -  description
			     -------------------
    begin		: Sat Oct 12 2002
    copyright	    : (C) 2002 by Ume� University, Sweden
    email		: roland@catalogix.se

   COPYING RESTRICTIONS APPLY.  See COPYRIGHT File in top level directory
   of this package for details.

 ***************************************************************************/

#include <config.h>

#include <string.h>

#include <macros.h>
#include <db0.h>
#include <struct.h>
#include <plugin.h>
#include <func.h>
#include <wrappers.h>
#include <spocp.h>
#include <dback.h>

#ifdef HAVE_LIBCRYPTO
#define uint8  unsigned char
#include <openssl/evp.h>
#else
#include <sha1.h>
#endif

/*#define AVLUS 1*/

static junc_t *
element_add(plugin_t * pl, junc_t * dvp, element_t * ep, ruleinst_t * ri, int n);
junc_t	*rm_next(junc_t * ap, branch_t * bp);
char	*set_item_list(junc_t * dv);
junc_t	*atom_add(branch_t * bp, atom_t * ap);
junc_t	*extref_add(branch_t * bp, atom_t * ap);
junc_t	*list_end(junc_t * arr);
static junc_t *
list_add(plugin_t * pl, branch_t * bp, list_t * lp, ruleinst_t * ri);
junc_t	*range_add(branch_t * bp, range_t * rp);
junc_t	*prefix_add(branch_t * bp, atom_t * ap);
junc_t	*rule_close(junc_t * ap, ruleinst_t * ri);

static	ruleinst_t *ruleinst_new(octet_t * rule, octet_t * blob, char *bcname);

ruleinst_t	*ruleinst_dup(ruleinst_t * ri);
void	ruleinst_free(ruleinst_t * ri);

static junc_t  *add_next(plugin_t *, junc_t *, element_t *, ruleinst_t *);
/*
 * ------------------------------------------------------------ 
 */

char	    item[NTYPES + 1];

/************************************************************
*							   *
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

junc_t	 *
junc_new()
{
	junc_t	 *ap;

	ap = (junc_t *) Calloc(1, sizeof(junc_t));

	return ap;
}

/************************************************************
*							   *
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

junc_t	 *
branch_add(junc_t * ap, branch_t * bp)
{
	if (ap == 0)
		ap = junc_new();

	ap->item[bp->type] = bp;
	bp->parent = ap;

	return ap;
}

/************************************************************
*							   *
************************************************************/

/*
static junc_t *
junc_set( junc_t *a, junc_t *b)
{
    int i;

    if (a == 0)
        a = junc_new();

	for (i = 0; i < NTYPES; i++) 
        a->item[i] = b->item[i];

    return a;
}
*/

/************************************************************
*						   &P_match_uid	*
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

junc_t	 *
junc_dup(junc_t * jp, ruleinfo_t * ri)
{
	junc_t	 *new;
	branch_t       *nbp, *obp;
	int	     i, j;

	if (jp == 0)
		return 0;

	new = junc_new();

	for (i = 0; i < NTYPES; i++) {
		if (jp->item[i]) {
			nbp = new->item[i] =
			    (branch_t *) Calloc(1, sizeof(branch_t));
			obp = jp->item[i];

			nbp->type = obp->type;
			nbp->count = obp->count;
			nbp->parent = new;

			switch (i) {
			case SPOC_ATOM:
				nbp->val.atom = phash_dup(obp->val.atom, ri);
				break;

			case SPOC_LIST:
				nbp->val.list = junc_dup(obp->val.list, ri);
				break;

			case SPOC_PREFIX:
				nbp->val.prefix = ssn_dup(obp->val.prefix, ri);
				break;

			case SPOC_SUFFIX:
				nbp->val.suffix = ssn_dup(obp->val.suffix, ri);
				break;

			case SPOC_RANGE:
				for (j = 0; j < DATATYPES; j++) {
					nbp->val.range[j] =
					    sl_dup(obp->val.range[j], ri);
				}
				break;

			case SPOC_ENDOFLIST:
				nbp->val.list = junc_dup(obp->val.list, ri);
				break;

			case SPOC_ENDOFRULE:
				nbp->val.id = index_dup(obp->val.id, ri);
				break;

				/*
				 * case SPOC_REPEAT : break ; 
				 */

			}
		}
	}

	return new;
}

/************************************************************
*							   *
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

char	   *
set_item_list(junc_t * dv)
{
	int	     i;

	for (i = 0; i < NTYPES; i++) {
		if (dv && dv->item[i])
			item[i] = '1';
		else
			item[i] = '0';
	}

	item[NTYPES] = '\0';

	return item;
}

/************************************************************
*							   *
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

/************************************************************
*							   *
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

junc_t	 *
atom_add(branch_t * bp, atom_t * ap)
{
	buck_t	 *bucket = 0;

	DEBUG(SPOCP_DSTORE) 
		oct_print(LOG_DEBUG,"atom_add", &ap->val);

	if (bp->val.atom == 0)
		bp->val.atom = phash_new(3, 50);

	bucket = phash_insert(bp->val.atom, ap, ap->hash);
	bucket->refc++;

	if (bucket->next == 0)
		bucket->next = junc_new();

	return bucket->next;
}


/************************************************************/

static junc_t  *
any_add(branch_t * bp)
{
	if (bp->val.next == 0)
		bp->val.next = junc_new();

	return bp->val.next;
}

/************************************************************/

/*
 * static char *P_print_junc( item_t i ) { junc_print( 0, (junc_t *) i ) ;
 * 
 * return 0 ; }
 * 
 * static void P_free_junc( item_t i ) { junc_free(( junc_t *) i ) ; }
 * 
 * static item_t P_junc_dup( item_t i, item_t j ) { return junc_dup( ( junc_t
 * * ) i , ( ruleinfo_t *) j ) ; } 
 */


/************************************************************
*							   *
************************************************************/

varr_t	 *
varr_ruleinst_add(varr_t * va, ruleinst_t * ju)
{
	return varr_add(va, (void *) ju);
}

ruleinst_t     *
varr_ruleinst_pop(varr_t * va)
{
	return (ruleinst_t *) varr_pop(va);
}

ruleinst_t     *
varr_ruleinst_nth(varr_t * va, int n)
{
	return (ruleinst_t *) varr_nth(va, n);
}

/************************************************************
*							   *
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

junc_t	 *
list_end(junc_t * arr)
{
	branch_t       *db;

	db = ARRFIND(arr, SPOC_ENDOFLIST);

	if (db == 0) {
		db = (branch_t *) Calloc(1, sizeof(branch_t));
		db->type = SPOC_ENDOFLIST;
		db->count = 1;
		db->val.list = junc_new();
		branch_add(arr, db);
	} else {
		db->count++;
	}

	DEBUG(SPOCP_DSTORE) traceLog(LOG_DEBUG,"List end [%d]", db->count);

	arr = db->val.list;

	return arr;
}

/************************************************************
*							   *
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

static junc_t	 *
list_add(plugin_t * pl, branch_t * bp, list_t * lp, ruleinst_t * ri)
{
	element_t      *elp;
	junc_t	 *jp;

	if (bp->val.list == 0) {
		jp = bp->val.list = junc_new();
	} else
		jp = bp->val.list;

	elp = lp->head;

	/*
	 * fails, means I should clean up 
	 */
	if ((jp = element_add(pl, jp, elp, ri, 1)) == 0) {
		traceLog(LOG_WARNING,"List add failed");
		return 0;
	}

	return jp;
}

/************************************************************
*							   *
************************************************************/

static void
list_clean(junc_t * jp, list_t * lp)
{
	element_t      *ep = lp->head, *next;
	buck_t	 *buck;
	branch_t       *bp;

	for (; ep; ep = next) {
		next = ep->next;
		if (jp == 0 || (bp = ARRFIND(jp, ep->type)) == 0)
			return;

		switch (ep->type) {
		case SPOC_ATOM:
			buck =
			    phash_search(bp->val.atom, ep->e.atom,
					 ep->e.atom->hash);
			jp = buck->next;
			buck->refc--;
			bp->count--;
			if (buck->refc == 0) {
				buck->val.val = 0;
				buck->hash = 0;
				junc_free(buck->next);
				buck->next = 0;
				next = 0;
			}
			break;

		case SPOC_LIST:
			bp->count--;
			if (bp->count == 0) {
				junc_free(bp->val.list);
				bp->val.list = 0;
			} else
				list_clean(bp->val.list, ep->e.list);
			break;
		}
	}
}

/************************************************************
*							   *
************************************************************/
/*
 * 
 * Arguments:
 * 
 * Returns: 
 */

junc_t	 *
range_add(branch_t * bp, range_t * rp)
{
	int	     i = rp->lower.type & 0x07;
	junc_t	 *jp;

	if (bp->val.range[i] == 0) {
		bp->val.range[i] = sl_init(4);
	}

	jp = sl_range_add(bp->val.range[i], rp);

	return jp;
}

/************************************************************
*	    adds a prefix to a prefix branch	       *
************************************************************/
/*
 * 
 * Arguments: bp pointer to a branch ap pointer to a atom
 * 
 * Returns: pointer to next node in the tree 
 */

junc_t	 *
prefix_add(branch_t * bp, atom_t * ap)
{
	/*
	 * assumes that the string to add is '\0' terminated 
	 */
	return ssn_insert(&bp->val.prefix, ap->val.val, FORWARD);
}

/************************************************************
*	    adds a suffix to a suffix branch	       *
************************************************************/
/*
 * 
 * Arguments: bp pointer to a branch ap pointer to a atom
 * 
 * Returns: pointer to next node in the tree 
 */

static junc_t  *
suffix_add(branch_t * bp, atom_t * ap)
{
	/*
	 * assumes that the string to add is '\0' terminated 
	 */
	return ssn_insert(&bp->val.suffix, ap->val.val, BACKWARD);
}

static junc_t *
set_add( branch_t *bp, element_t *ep, plugin_t *pl, junc_t *jp, ruleinst_t *rt)
{
	int		n,i;
	junc_t		*ap = 0/*, *rp=0*/;
	varr_t		*va, *dsva;
	element_t	*elem;
	dset_t		*ds;
	void		*v;

	va = ep->e.set;
	n = varr_len(va);
	if (bp->val.set == 0)
		ds = bp->val.set =
		    (dset_t *) Calloc(1, sizeof(dset_t));
	else {
		for (ds = bp->val.set; ds->next; ds = ds->next);
		ds->next = (dset_t *) Calloc(1, sizeof(dset_t));
		ds = ds->next;
	}
	ds->uid = rt->uid;

	for (v = varr_first(va), dsva = ds->va, i=0; v;
	     v = varr_next(va, v), i++) {
		DEBUG(SPOCP_DSTORE) traceLog(LOG_DEBUG, "- set element %d -", i);
		elem = (element_t *) v;
		if ((ap = element_add(pl, jp, elem, rt, 1)) == 0)
			break;

#ifdef AVLUS
        if (i==0) {
			junc_print_r( 2, jp);
        }
#endif

		dsva = varr_add(dsva, ap);
	}
	ds->va = dsva;


	return ap;
}

/************************************************************
*      Is this the last element in a S-expression ?	 *
************************************************************/
/*
 * assumes list as most basic unit
 * 
 * Arguments: ep element
 * 
 * Returns: 1 if it is the last element otherwise 0
 * 
 * static int last_element( element_t *ep ) { while( ep->next == 0 &&
 * ep->memberof != 0 ) ep = ep->memberof ;
 * 
 * if( ep->next == 0 && ep->memberof == 0 ) return 1 ; else return 0 ; } 
 */

/************************************************************
*	   Adds end-of-rule marker to the tree	     *
************************************************************/
/*
 * 
 * Arguments: ap node in the tree id ID for this rule
 * 
 * Returns: pointer to next node in the tree 
 */

static junc_t  *
rule_end(junc_t * ap, ruleinst_t * ri)
{
	branch_t       *bp;

#ifdef AVLUS
	junc_print( 0, ap );
#endif

	if ((bp = ARRFIND(ap, SPOC_ENDOFRULE)) == 0) {
		DEBUG(SPOCP_DSTORE)
			traceLog(LOG_DEBUG,"New rule end");

		bp = (branch_t *) Calloc(1, sizeof(branch_t));
		bp->type = SPOC_ENDOFRULE;
		bp->count = 1;
		bp->val.id = index_add(bp->val.id, (void *) ri);

		ap = branch_add(ap, bp);
	} else {		/* If rules contains references this can
				 * happen, otherwise not */
		bp->count++;
		DEBUG(SPOCP_DSTORE)
			traceLog(LOG_DEBUG,"Old rule end: count [%d]",
					     bp->count);
		bp->val.id = index_add(bp->val.id, (void *) ri);
#ifdef AVLUS
		index_print( bp->val.id );
#endif
	}

	DEBUG(SPOCP_DSTORE)
		traceLog(LOG_DEBUG,"done rule %s", ri->uid);

	return ap;
}

/************************************************************
*	   Adds end-of-list marker to the tree	     *
************************************************************/
/*
 * 
 * Arguments: ap node in the tree ep end of list element id ID for this rule
 * 
 * Returns: pointer to next node in the tree
 * 
 * Should mark in some way that end of rule has been reached 
 */

static junc_t  *
list_close(junc_t * ap, element_t **epp, ruleinst_t * ri, int *eor)
{
	element_t      *parent, *ep = *epp;

	do {
		parent = ep->memberof;

		if (parent->type == SPOC_LIST) 
			ap = list_end(ap);

		DEBUG(SPOCP_DSTORE) {
			if (parent->type == SPOC_LIST) {
				oct_print(LOG_DEBUG,
					"end_list that started with",
					&parent->e.list->head->e.atom->val);
			}
			else if(parent->type == SPOC_SET) 
				traceLog(LOG_DEBUG,"Set end");
			else
				traceLog(LOG_DEBUG,"end of type=%d", parent->type); 
		}

		ep = parent;

#ifdef AVLUS
		DEBUG(SPOCP_DSTORE)
			traceLog(LOG_DEBUG,"type:%d next:%p memberof:%p",
				ep->type, ep->next, ep->memberof);
#endif

	} while ((ep->type == SPOC_LIST || ep->type == SPOC_SET)
			&& ep->next == 0 && ep->memberof != 0);

	*epp = parent;
	if (parent->memberof == 0) {
		ap = rule_end(ap, ri);
		*eor = 1;
	} else
		*eor = 0;

	return ap;
}

static junc_t  *
add_next(plugin_t * plugin, junc_t * jp, element_t * ep, ruleinst_t * ri)
{
	int	     eor = 0;

#ifdef AVLUS
	{
		octet_t *eop ;

		eop = oct_new( 512,NULL);
		element_print( eop, ep );
		oct_print( LOG_INFO, "add_next() after", eop);
		oct_free( eop);
	}
#endif

	if (ep->next) {
		DEBUG(SPOCP_DSTORE)
			traceLog( LOG_DEBUG, "next exists");
		jp = element_add(plugin, jp, ep->next, ri, 1);
	}
	else if (ep->memberof) {
		DEBUG(SPOCP_DSTORE)
			traceLog( LOG_DEBUG, "No next exists");
		/*
		 * if( ep->memberof->type == SPOC_SET ) ; 
		 */
		
		if (ep->type != SPOC_LIST)	/* a list never closes itself */
			jp = list_close(jp, &ep, ri, &eor);

		/* ep is now one of the parents of the original ep */
		if ( !eor && ep->next) {
			/*traceLog( LOG_DEBUG, "next exists");*/
			jp = element_add(plugin, jp, ep->next, ri, 1);
		}
	}

	return jp;
}

/************************************************************
*	   Add a s-expression element		      *
************************************************************/
/*
 * The representation of the S-expression representing rules are as a tree. A
 * every node in the tree there are a couple of different types of branches
 * that can appear. This routine chooses which type it should be and then
 * adds this element to that branch.
 * 
 * Arguments: ap node in the tree ep element to store rt pointer to the
 * ruleinstance
 * 
 * Returns: pointer to the next node in the tree 
 */


junc_t	 *
element_add(plugin_t * pl, junc_t * jp, element_t * ep, ruleinst_t * rt,
	    int next)
{
	branch_t	*bp = 0;
	junc_t		*ap = 0;

	bp = ARRFIND(jp, ep->type);

#ifdef AVLUS
	{
		octet_t *eop ;

		eop = oct_new( 512,NULL);
		element_print( eop, ep );
		oct_print( LOG_INFO, "element_add", eop);
		oct_free( eop);
	}
#endif

	if (bp == 0) {
		bp = (branch_t *) Calloc(1, sizeof(branch_t));
		bp->type = ep->type;
		bp->count = 1;
		ap = branch_add(jp, bp);
	} else {
		bp->count++;
	}

	if (ep->type == SPOC_SET){
		ap = set_add(bp, ep, pl, jp, rt);
	}
	else {
		switch (ep->type) {
		case SPOC_ATOM:
			ap = atom_add(bp, ep->e.atom);
			break;

		case SPOC_PREFIX:
			ap = prefix_add(bp, ep->e.atom);
			break;

		case SPOC_SUFFIX:
			ap = suffix_add(bp, ep->e.atom);
			break;

		case SPOC_RANGE:
			ap = range_add(bp, ep->e.range);
			break;

		case SPOC_LIST:
			ap = list_add(pl, bp, ep->e.list, rt);
			break;

		case SPOC_ANY:
			ap = any_add(bp);
			break;
		}

		if (ap && next)
			ap = add_next(pl, ap, ep, rt);
		
	}


	if (ap == 0 && bp != 0) {
		bp->count--;

		if (bp->count == 0) {
			DEBUG(SPOCP_DSTORE)
				traceLog( LOG_DEBUG, "Freeing type %d branch",
				     bp->type);
			branch_free(bp);
			jp->item[ep->type] = 0;
		}
	}

	return ap;
}

/************************************************************
		 RULE INFO functions 
 ************************************************************/

/*
 * --------------- ruleinst ----------------------------- 
 */

unsigned int
ruleinst_uid(unsigned char *sha1sum, octet_t * rule, octet_t * blob, char *bcname) 
{
#ifdef HAVE_LIBCRYPTO
	EVP_MD_CTX			ctx;	
	const EVP_MD		*md;
#else
	struct sha1_context	ctx;
#endif

	unsigned int		l;

#ifdef HAVE_LIBCRYPTO
	md = EVP_sha1();
	
	EVP_DigestInit(&ctx, md);
	EVP_DigestUpdate(&ctx,(uint8 *) rule->val, rule->len);
	if (bcname)
		EVP_DigestUpdate(&ctx, (uint8 *) bcname, strlen(bcname));
	if (blob)
		EVP_DigestUpdate(&ctx, (uint8 *) blob->val, blob->len);
	EVP_DigestFinal(&ctx, sha1sum, &l);
#else
	sha1_starts(&ctx);

	sha1_update(&ctx, (uint8 *) rule->val, rule->len);
	if (bcname)
		sha1_update(&ctx, (uint8 *) bcname, strlen(bcname));
	if (blob)
		sha1_update(&ctx, (uint8 *) blob->val, blob->len);

	sha1_finish(&ctx, (unsigned char *) sha1sum);
	l = 20;
#endif

	return l;
}

static ruleinst_t *
ruleinst_new(octet_t * rule, octet_t * blob, char *bcname)
{
	ruleinst_t     *rip;
#ifdef HAVE_LIBCRYPTO	
	unsigned char   sha1sum[EVP_MAX_MD_SIZE];
#else
	unsigned char   sha1sum[21];
#endif
	unsigned char	*ucp;
	int				j;
	unsigned int	l;

	if (rule == 0 || rule->len == 0)
		return 0;

	rip = (ruleinst_t *) Calloc(1, sizeof(ruleinst_t));

	rip->rule = octdup(rule);

	if (blob && blob->len)
		rip->blob = octdup(blob);
	else
		rip->blob = 0;

	memset(sha1sum,0,20);
	
	l = ruleinst_uid( sha1sum, rule, blob, bcname );

	for (j = 0, ucp = (unsigned char *) rip->uid; j < 20; j++, ucp += 2)
		sprintf((char *) ucp, "%02x", sha1sum[j]);

	DEBUG(SPOCP_DSTORE) traceLog(LOG_DEBUG,"New rule (%s)", rip->uid);

	rip->ep = 0;
	rip->alias = 0;

	return rip;
}

ruleinst_t     *
ruleinst_dup(ruleinst_t * ri)
{
	ruleinst_t     *new;

	if (ri == 0)
		return 0;

	if (ri->bcond)
		new = ruleinst_new(ri->rule, ri->blob, ri->bcond->name);
	else
		new = ruleinst_new(ri->rule, ri->blob, 0);

	new = (ruleinst_t *) Calloc(1, sizeof(ruleinst_t));

	/* Flawfinder: ignore */
	strcpy(new->uid, ri->uid);

	new->rule = octdup(ri->rule);
	/*
	 * if( ri->ep ) new->ep = element_dup( ri->ep ) ; 
	 */
	if (ri->alias)
		new->alias = ll_dup(ri->alias);

	return new;
}

static item_t
P_ruleinst_dup(item_t i, item_t j)
{
	return (void *) ruleinst_dup((ruleinst_t *) i);
}

void
ruleinst_free(ruleinst_t * rt)
{
	if (rt) {
		if (rt->rule)
			oct_free(rt->rule);
		if (rt->bcexp)
			oct_free(rt->bcexp);
		if (rt->blob)
			oct_free(rt->blob);
		if (rt->alias)
			ll_free(rt->alias);
		/*
		 * if( rt->ep ) element_rm( rt->ep ) ; 
		 */

		Free(rt);
	}
}

/*
 * static int uid_match( ruleinst_t *rt, char *uid ) { * traceLog( "%s <> %s", 
 * rt->uid, uid ) ; *
 * 
 * return strcmp( rt->uid, uid ) ; }
 * 
 * static int P_match_uid( void *vp, void *pattern ) { return uid_match(
 * (ruleinst_t *) vp, (char *) pattern ) ; } 
 */

/*
 * --------- ruleinst functions ----------------------------- 
 */

static void
P_ruleinst_free(void *vp)
{
	ruleinst_free((ruleinst_t *) vp);
}

static int
P_ruleinst_cmp(void *a, void *b)
{
	ruleinst_t     *ra, *rb;

	if (a == 0 && b == 0)
		return 0;
	if (a == 0 || b == 0)
		return 1;

	ra = (ruleinst_t *) a;
	rb = (ruleinst_t *) b;

	return strcmp(ra->uid, rb->uid);
}

/*
 * PLACEHOLDER 
 */
static char    *
P_ruleinst_print(void *vp)
{
	return Strdup((char *) vp);
}

static Key
P_ruleinst_key(item_t i)
{
	ruleinst_t     *ri = (ruleinst_t *) i;

	return ri->uid;
}


/*
 * --------- ruleinfo ----------------------------- 
 */

ruleinfo_t     *
ruleinfo_new(void)
{
	return (ruleinfo_t *) Calloc(1, sizeof(ruleinfo_t));
}

ruleinfo_t     *
ruleinfo_dup(ruleinfo_t * ri)
{
	ruleinfo_t     *new;

	if (ri == 0)
		return 0;

	new = (ruleinfo_t *) Calloc(1, sizeof(ruleinfo_t));
	new->rules = rdb_dup(ri->rules, 1);

	return new;
}

void
ruleinfo_free(ruleinfo_t * ri)
{
	if (ri) {
		rdb_free(ri->rules);
		Free(ri);
	}
}

ruleinst_t     *
ruleinst_find_by_uid(void * rules, char *uid)
{
	if (rules == NULL)
		return NULL;

	return (ruleinst_t *) rdb_search(rules, uid);
}

/*
 * ---------------------------------------- 
 */

/*
 * void rm_raci( ruleinst_t *rip, raci_t *ap ) { }
 * 
 */

int
free_rule(ruleinfo_t * ri, char *uid)
{
	int r;
	
	r = rdb_remove(ri->rules, uid);
	rdb_print( ri->rules);
	return r;
}

void
free_all_rules(ruleinfo_t * ri)
{
	ruleinfo_free(ri);
}

static ruleinst_t *
save_rule(db_t * db, dbcmd_t * dbc, octet_t * rule, octet_t * blob,
	  char *bcondname)
{
	ruleinfo_t     *ri;
	ruleinst_t     *rt;
	int				r;

	if (db->ri == 0)
		db->ri = ri = ruleinfo_new();
	else
		ri = db->ri;

	rt = ruleinst_new(rule, blob, bcondname);

	if (ri->rules == 0)
		ri->rules =
		    rdb_new(&P_ruleinst_cmp, &P_ruleinst_free,
			     &P_ruleinst_key, &P_ruleinst_dup,
			     &P_ruleinst_print);
	else {
		if (ruleinst_find_by_uid(ri->rules, rt->uid) != 0) {
			ruleinst_free(rt);
			return 0;
		}
	}

	if (dbc && dbc->dback) {
		traceLog(LOG_DEBUG, "Got persistent store");
		dback_save(dbc, rt->uid, rule, blob, bcondname);
	}

	r = rdb_insert(ri->rules, (item_t) rt);
	DEBUG(SPOCP_DSTORE) {
		traceLog(SPOCP_DEBUG,"rdb_insert %d", r);
		rdb_print( ri->rules );
	}

	return rt;
}

int
nrules(ruleinfo_t * ri)
{
	if (ri == 0)
		return 0;
	else
		return rdb_rules(ri->rules);
}

int
rules(db_t * db)
{
	if (db == 0 || nrules(db->ri) == 0 )
		return 0;
	else
		return 1;
}

ruleinst_t     *
get_rule(ruleinfo_t * ri, octet_t *oct)
{
	char	uid[41];

	if (ri == 0 || oct == 0 )
		return 0;

	if (oct2strcpy( oct, uid, 41, 0 ) < 0 ) 
		return NULL;

	return ruleinst_find_by_uid(ri->rules, uid);
}

/*
 *! \brief
 * 	creates an output string that looks like this
 * 
 * path ruleid rule [ boundarycond [ blob ]]
 *
 * If no boundary condition is defined but a blob is, then
 * the boundary condition is represented with "NULL"
 * \param r Pointer to the ruleinstance
 * \param rs The name of the ruleset
 * \returns Pointer to a newly allocated octet containing the output string
 */
octet_t	*
ruleinst_print(ruleinst_t * r, char *rs)
{
	octet_t	*oct;
	int	     l, lr, bc=0;
	char	    flen[1024];


	/* First the path */
	if (rs && strlen(rs) ) {
		lr = strlen(rs);
		snprintf(flen, 1024, "%d:%s", lr, rs);
	} else {
		strcpy(flen, "1:/");
		lr = 1;
	}

	/* calculate the total length of the whole string */
	/* starting point path, ruleid and rule only */
	l = r->rule->len + DIGITS(r->rule->len) + lr + DIGITS(lr) + 5 + 40 + 1;

	if (r->bcexp && r->bcexp->len) {
		l += r->bcexp->len + 1 + DIGITS(r->bcexp->len);
		bc = 2;
	}

	if (r->blob && r->blob->len) {
		l += r->blob->len + 1 + DIGITS(r->blob->len);
		if (bc == 0) {
			l += 6;
			bc = 1;
		}
	}

	oct = oct_new(l, 0);

	octcat(oct, flen, strlen(flen));

	octcat(oct, "40:", 3);

	octcat(oct, r->uid, 40);

	snprintf(flen, 1024, "%d:", (int) r->rule->len);
	octcat(oct, flen, strlen(flen));

	octcat(oct, r->rule->val, r->rule->len);

	if( bc == 2) {
		snprintf(flen, 1024, "%d:", (int) r->bcexp->len);
		octcat(oct, flen, strlen(flen));
		octcat(oct, r->bcexp->val, r->bcexp->len);
	}
	else if (bc==1)
		octcat( oct, "4:NULL", 6 );

	if (r->blob && r->blob->len) {
		snprintf(flen, 1024, "%d:", (int) r->blob->len);
		octcat(oct, flen, strlen(flen));
		octcat(oct, r->blob->val, r->blob->len);
	}

	return oct;
}

spocp_result_t
get_all_rules(db_t * db, octarr_t * oa, char *rs)
{
	int	     i, n;
	ruleinst_t     *r;
	varr_t	 *pa = 0;
	octet_t	*oct;
	spocp_result_t  rc = SPOCP_SUCCESS;

	n = nrules(db->ri);

	if (n == 0)
		return rc;

	/* resize if too small */
	if ((oa && (oa->size - oa->n) < n))
		octarr_mr(oa, n);

	pa = rdb_all(db->ri->rules);

	for (i = 0; (r = (ruleinst_t *) varr_nth(pa, i)); i++) {

		/*
#ifdef AVLUS
		 {
			char	*str;
			traceLog("...") ;
			str = oct2strdup( r->rule, '%' );
			traceLog(LOG_DEBUG,"Rule[%d]: %s", i, str );
			Free(str);
		}
		*/

		if ((oct = ruleinst_print(r, rs)) == 0) {
			rc = SPOCP_OPERATIONSERROR;
			octarr_free(oa);
			break;
		}

		/*
#ifdef AVLUS
		 {
			char	*str;
			str = oct2strdup( oct, '%' );
			traceLog(LOG_DEBUG,"Rule[%d] => %s", i, str );
			Free(str);
		}
		*/

		oa = octarr_add(oa, oct);

		/*
#ifdef AVLUS
			traceLog(LOG_DEBUG,"Added to octarr") ;
		*/
	}

	/*
	 * dont't remove the items since I've only used pointers 
	 */
	varr_free(pa);

	return rc;
}

octet_t	*
get_blob(ruleinst_t * ri)
{

	if (ri == 0)
		return 0;
	else
		return ri->blob;
}

db_t	   *
db_new()
{
	db_t	   *db;

	db = (db_t *) Calloc(1, sizeof(db_t));
	db->jp = junc_new();
	db->ri = ruleinfo_new();

	return db;
}

void
db_clr( db_t *db)
{
	if (db) {
		junc_free( db->jp );
		ruleinfo_free( db->ri );
/*		bcdef_free( db->bcdef ); */
		/* keep the plugins */
	}
}
void
db_free( db_t *db)
{
	if (db) {
		junc_free( db->jp );
		ruleinfo_free( db->ri );
/*		bcdef_free( db->bcdef ); */
		plugin_unload_all( db->plugins );

		Free(db);
	}
}

/************************************************************
*    Add a rightdescription to the rules database	   *
************************************************************/
/*
 * 
 * Arguments: db Pointer to the incore representation of the rules database ep 
 * Pointer to a parsed S-expression rt Pointer to the rule instance
 * 
 * Returns: TRUE if OK 
 */

static spocp_result_t
store_right(db_t * db, element_t * ep, ruleinst_t * rt)
{
	int	     r;
	element_t	*ec;

	if (db->jp == 0)
		db->jp = junc_new();

	ec = element_dup( ep, NULL );

#ifdef AVLUS
	{
		octet_t *eop ;

		eop = oct_new( 512,NULL);
		element_struct( eop, ec );
		oct_print( LOG_INFO, "struct", eop);
		oct_free( eop);
	}
#endif

	if (element_add(db->plugins, db->jp, ep, rt, 1) == 0)
		r = SPOCP_OPERATIONSERROR;
	else {
		rt->ep = ec;
		r = SPOCP_SUCCESS;
	}

	return r;
}

/************************************************************
*    Store a rights description in the rules database	   *
************************************************************/
/*
 * 
 * Arguments: db Pointer to the incore representation of the rules database
 * 
 * Returns: TRUE if OK 
 */

spocp_result_t
add_right(db_t ** db, dbcmd_t * dbc, octarr_t * oa, ruleinst_t ** ri,
	  bcdef_t * bcd)
{
	element_t      *ep;
	octet_t	 rule, blob, oct;
	ruleinst_t     *rt;
	spocp_result_t  rc = SPOCP_SUCCESS;

	/*
	 * LOG( SPOCP_WARNING )
		traceLog(LOG_WARNING,"Adding new rule: \"%s\"", rp->val) ; 
	 */

	rule.len = rule.size = 0;
	rule.val = 0;

	blob.len = blob.size = 0;
	blob.val = 0;

	octln(&oct, oa->arr[0]);
	octln(&rule, oa->arr[0]);

	if (oa->n > 1) {
		octln(&blob, oa->arr[1]);
	} else
		blob.len = 0;

	if ((rc = element_get(&oct, &ep)) == SPOCP_SUCCESS) {

		/*
		 * stuff left ?? 
		 */
		/*
		 * just ignore it 
		 */
		if (oct.len) {
			rule.len -= oct.len;
		}

		if ((rt =
		     save_rule(*db, dbc, &rule, &blob,
			       bcd ? bcd->name : NULL)) == 0) {
			element_free(ep);
			return SPOCP_EXISTS;
		}

		/*
		 * right == rule 
		 */
		if ((rc = store_right(*db, ep, rt)) != SPOCP_SUCCESS) {
			element_free(ep);

			/*
			 * remove ruleinstance 
			 */
			free_rule((*db)->ri, rt->uid);

			LOG(SPOCP_WARNING)
				traceLog(LOG_WARNING,"Error while adding rule");
		}

		if (bcd) {
			rt->bcond = bcd;
			bcd->rules = varr_add(bcd->rules, (void *) rt);
		}

		*ri = rt;
#ifdef AVLUS
		junc_print_r(0, (*db)->jp);
#endif
	}

	return rc;
}

/*
 * static int ruleinst_print( ruleinst_t *ri ) { char *str ;
 * 
 * if( ri == 0 ) return 0 ; traceLog( "uid: %s", ri->uid ) ;
 * 
 * str = oct2strdup( ri->rule, '%' ) ; traceLog( "rule: %s", str ) ;
 * 
 * if( ri->blob ) { str = oct2strdup( ri->blob, '%' ) ; traceLog( "rule: %s",
 * str ) ; }
 * 
 * if( ri->alias ) ll_print( ri->alias ) ;
 * 
 * return 0 ; } 
 */

int
ruleinfo_print(ruleinfo_t * r)
{
	if (r == 0)
		return 0;

	rdb_print(r->rules);

	return 0;
}
