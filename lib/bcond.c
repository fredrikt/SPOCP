/*! \file lib/bcond.c \author Roland Hedberg <roland@catalogix.se> 
 */

#include <string.h>

#include <db0.h>
#include <wrappers.h>

#ifdef HAVE_LIBCRYPTO
#include <openssl/evp.h>
#else
#include <sha1.h>
#endif

#include <func.h>
#include <plugin.h>
#include <spocp.h>
#include <rvapi.h>
#include <proto.h>
#include <dback.h>

/* #define AVLUS 1 */

/*! Local struct used to store parsed information about bcond s-expressions */
typedef struct _stree {
	/*! Is this a list/set or not */
	int             list;
	/*! The atom */
	octet_t         val;
	/*! Next element */
	struct _stree  *next;
	/*! Previous element */
	struct _stree  *part;
} stree_t;

bcexp_t *
transv_stree(plugin_t * plt, stree_t * st, bcdef_t * list, bcdef_t * parent);

/*
 * ====================================================================== *
 * LOCAL FUNCTIONS
 * ====================================================================== 
 */

static bcspec_t *
bcspec_new(plugin_t * plt, octet_t * spec)
{
	octet_t         oct;
	bcspec_t       *bcs = 0;
	plugin_t       *p;
	int             n;

	/*
	 * two parts name ":" typespec 
	 */
	if ((n = octchr(spec, ':')) < 0) {
		oct_print( LOG_ERR,
		    "Error in boundary condition specification", spec);
		return 0;
	}

	oct.len = spec->len - n - 1;
	oct.val = spec->val + n + 1;
	spec->len = n;

	if ((p = plugin_match(plt, spec)) == 0) {
		traceLog(LOG_ERR,"Doesn't match any known plugin",spec);
		return 0;
	}

	bcs = (bcspec_t *) Calloc(1,sizeof(bcspec_t));

	bcs->plugin = p;
	bcs->name = oct2strdup(spec, 0);
	bcs->spec = octdup(&oct);

	return bcs;
}

/*
 * ---------------------------------------------------------------------- 
 */

static void
bcspec_free(bcspec_t * bcs)
{
	if (bcs) {
		if (bcs->name)
			Free(bcs->name);
		if (bcs->args)
			octarr_free(bcs->args);
		if (bcs->spec)
			oct_free(bcs->spec);

		Free(bcs);
	}
}

/*
 * ---------------------------------------------------------------------- 
 */

static bcexp_t *
bcexp_new()
{
	return (bcexp_t *) Calloc(1, sizeof(bcexp_t));
}

/*
 * ---------------------------------------------------------------------- 
 */

static bcexp_t *
varr_bcexp_pop(varr_t * va)
{
	return (bcexp_t *) varr_pop(va);
}

/*
 * ---------------------------------------------------------------------- 
 */

static void
bcexp_free(bcexp_t * bce)
{
	bcexp_t        *bp;

	if (bce) {
		switch (bce->type) {
		case AND:
		case OR:
			while ((bp = varr_bcexp_pop(bce->val.arr)) != 0)
				bcexp_free(bp);
			if (bce->val.arr)
				Free(bce->val.arr);
			break;

		case NOT:
			bcexp_free(bce->val.single);
			break;

		case REF:
			break;

		case SPEC:
			bcspec_free(bce->val.spec);
		default:
			break;

		}
		Free(bce);
	}
}

/*
 * ---------------------------------------------------------------------- 
 */

void
bcdef_free(bcdef_t * bcd)
{
	bcexp_t        *bce;

	if (bcd) {
		if (bcd->name)
			Free(bcd->name);
		if (bcd->exp)
			bcexp_free(bcd->exp);
		while (bcd->users && bcd->users->n) {
			bce = varr_bcexp_pop(bcd->users);
			/*
			 * don't free the bce it's just a pointer 
			 */
		}

		if (bcd->users)
			Free(bcd->users);
	}
}

/*
 * ---------------------------------------------------------------------- 
 */

static bcdef_t *
bcdef_new(void)
{
	bcdef_t        *new;

	new = (bcdef_t *) Calloc(1, sizeof(bcdef_t));

	return new;
}

/*
 * ---------------------------------------------------------------------- 
 */

static bcdef_t *
bcdef_find(bcdef_t * bcd, octet_t * pattern)
{
	for (; bcd; bcd = bcd->next)
		if (oct2strcmp(pattern, bcd->name) == 0)
			return bcd;

	return 0;
}

/*
 * ---------------------------------------------------------------------- 
 */

static bcdef_t *
bcdef_rm(bcdef_t * root, bcdef_t * rm)
{
	if (rm == root) {
		if (root->next) {
			root->next->prev = 0;
			return root->next;
		}
		return 0;
	}

	if (rm->next)
		rm->next->prev = rm->prev;
	if (rm->prev)
		rm->prev->next = rm->next;

	return root;
}

/*
 * ---------------------------------------------------------------------- 
 */

#ifdef HAVE_LIBCRYPTO
#define uint8  unsigned char
#endif

static char    *
make_name(char *str, octet_t * input)
{
#ifdef HAVE_LIBCRYPTO
	EVP_MD_CTX ctx;	
	const EVP_MD *md;
	unsigned char   sha1sum[EVP_MAX_MD_SIZE], *ucp;
#else
	struct sha1_context ctx;
	unsigned char   sha1sum[21], *ucp;
#endif

	int             j;
	unsigned int	l;


#ifdef HAVE_LIBCRYPTO
	traceLog(LOG_DEBUG,"Using EVP_sha1 to create unique identifier (bcond)");
	md = EVP_sha1();
	
	EVP_DigestInit(&ctx, md);
	EVP_DigestUpdate(&ctx,(uint8 *) input->val, input->len);
	EVP_DigestFinal(&ctx, sha1sum, &l);
#else
	sha1_starts(&ctx);

	sha1_update(&ctx, (uint8 *) input->val, input->len);

	sha1_finish(&ctx, (unsigned char *) sha1sum);
#endif

	for (j = 0, ucp = (unsigned char *) str+1; j < 20; j++, ucp += 2)
		sprintf((char *) ucp, "%02x", sha1sum[j]);

	str[0] = '_';
	str[41] = '_';
	str[42] = '\0';

	return str;
}

/*
 * ----------------------------------------------------------------------------- 
 */

static void
stree_free(stree_t * stp)
{
	if (stp) {
		if (stp->part)
			stree_free(stp->part);
		if (stp->next)
			stree_free(stp->next);
		if (stp->val.size)
			Free(stp->val.val);
		Free(stp);
	}
}

/*
 * bcond = bcondexpr / bcondref
 * 
 * bcondexpr = bcondspec / bcondvar
 * 
 * bcondvar = bcondor / bcondand / bcondnot / bcondref
 * 
 * bcondspec = bcondname ":" typespecific
 * 
 * bcondname = 1*dirchar
 * 
 * typespecific = octetstring
 * 
 * bcondref = "(" "ref" 1*dirchar ")"
 * 
 * bcondand = "(" "and" 1*bcondvar ")"
 * 
 * bcondor = "(" "or" 1*bcondvar ")"
 * 
 * bcondnot = "(" "not" bcondvar ")"
 * 
 * arg = ( path / search ) [ "*" / intervall ]
 * 
 * intervall = "[" single / prefix / suffix / both "]"
 * 
 * single = num prefix = num "-" suffix = "-" ( num / "last" ) both = num
 * "-" ( num / "last" ) num = 1*DIG DIG = %x30-39 
 */

static stree_t *
parse_bcexp(octet_t * sexp)
{
	stree_t	*ptp, *ntp = 0, *ptr;

	if (*sexp->val == '(') {
		ptp = (stree_t *) Calloc(1, sizeof(stree_t));
		ptp->list = 1;
		sexp->val++;
		sexp->len--;
		while (sexp->len && *sexp->val != ')') {
			if ((ptr = parse_bcexp(sexp)) == 0) {
				stree_free(ptp);
				return 0;
			}
 
			if (ptp->part == 0) {
				if (ptr->list == 0) {
					if (oct2strcmp( &ptr->val, "ref") == 0 );
					else if (oct2strcmp( &ptr->val, "and") == 0 );
					else if (oct2strcmp( &ptr->val, "or") == 0 );
					else if (oct2strcmp( &ptr->val, "not") == 0 );
					else {
						stree_free(ptr);
						return 0;
					}
				}
				else {
					stree_free(ptr);
					return 0;
				}

				ntp = ptp->part = ptr;
			} else {
				ntp->next = ptr;
				ntp = ntp->next;
			}
		}

		if (*sexp->val == ')') {
			sexp->val++;
			sexp->len--;
		} else {	/* error */
			stree_free(ptp);
			return 0;
		}
	} else {
		ptp = (stree_t *) Calloc(1, sizeof(stree_t));
		if (get_str(sexp, &ptp->val) != SPOCP_SUCCESS) {
			stree_free(ptp);
			return 0;
		}
	}

	return ptp;
}

/*
 * ---------------------------------------------------------------------- 
 */
/*!
 * \brief Translates a stree representation of a boundary condition expression into
 *   a bcexp struct
 * \param plt Pointer to the list of known plugins
 * \param st The stree that is to be turned into a bounday condition definition
 * \param list The list of the present boundary condition definitions
 * \param parent
 * \return a struct representing a boundary expression
 */
bcexp_t *
transv_stree(plugin_t * plt, stree_t * st, bcdef_t * list, bcdef_t * parent)
{
	bcexp_t        *bce, *tmp;
	bcdef_t        *bcd;
	bcspec_t       *bcs;

	if (!st->list) {
		if (!st->next) {	/* should be a bcond spec */
			if (plt == 0 || (bcs = bcspec_new(plt, &st->val)) == 0) {
				if (plt == 0)
					traceLog(LOG_ERR,
					    "Reference to plugin while non is defined");
				return 0;
			}
			bce = bcexp_new();
			bce->type = SPEC;
			bce->parent = parent;
			bce->val.spec = bcs;
		} else {
			if (oct2strcmp(&st->val, "ref") == 0) {
				/*
				 * if the ref isn't there this def isn't valid 
				 */
				if ((bcd =
				     bcdef_find(list, &st->next->val)) == 0)
					return 0;
				bce = bcexp_new();
				bce->type = REF;
				bce->parent = parent;
				bce->val.ref = bcd;
				/*
				 * so I know who to notify if anything happens 
				 * to this ref 
				 */
				bcd->users = varr_add(bcd->users, bce);
			} else if (oct2strcmp(&st->val, "and") == 0) {
				bce = bcexp_new();
				bce->type = AND;
				bce->parent = parent;
				for (st = st->next; st; st = st->next) {
					if ((tmp =
					     transv_stree(plt, st, list,
							  parent)) == 0) {
						bcexp_free(bce);
						return 0;
					}
					bce->val.arr =
					    varr_add(bce->val.arr,
						     (void *) tmp);
				}
			} else if (oct2strcmp(&st->val, "or") == 0) {
				bce = bcexp_new();
				bce->type = OR;
				bce->parent = parent;
				for (st = st->next; st; st = st->next) {
					if ((tmp =
					     transv_stree(plt, st, list,
							  parent)) == 0) {
						bcexp_free(bce);
						return 0;
					}
					bce->val.arr =
					    varr_add(bce->val.arr,
						     (void *) tmp);
				}
			} else if (oct2strcmp(&st->val, "not") == 0) {
				if (st->next->next)
					return 0;
				bce = bcexp_new();
				bce->type = NOT;
				bce->parent = parent;
				if ((bce->val.single =
				     transv_stree(plt, st->next, list,
						  parent)) == 0) {
					Free(bce);
					return 0;
				}
			} else
				return 0;
		}

		return bce;
	} else
		return transv_stree(plt, st->part, list, parent);
}

/*
 * ---------------------------------------------------------------------- 
 */

static void
bcexp_deref(bcexp_t * bce)
{
	bcdef_t        *bcd;

	bcd = bce->val.ref;

	bcd->users = varr_rm(bcd->users, bce);
}

/*
 * ---------------------------------------------------------------------- 
 */

/*
 * recursively find all the rules that uses this boundary condition directly
 * or through some intermediate 
 */
static varr_t  *
all_rule_users(bcdef_t * head, varr_t * rules)
{
	void           *vp;
	bcdef_t        *bcd;

	if (head->users) {
		for (vp = varr_first(head->users); vp;
		     vp = varr_next(head->users, vp)) {
			bcd = (bcdef_t *) vp;
			if (bcd->users || bcd->rules)
				rules = all_rule_users(bcd, rules);
		}
	}

	if (head->rules)
		rules = varr_or(rules, head->rules, 1);

	return rules;
}

/*
 * ---------------------------------------------------------------------- 
 */

static void
bcexp_delink(bcexp_t * bce)
{
	int             i, n;
	varr_t         *va;

	switch (bce->type) {
	case SPEC:
		break;

	case AND:
	case OR:
		va = bce->val.arr;
		n = varr_len(va);
		for (i = 0; i < n; i++)
			bcexp_delink((bcexp_t *) varr_nth(va, i));
		break;

	case NOT:
		bcexp_delink(bce->val.single);
		break;

	case REF:
		bcexp_deref(bce);
		break;

	}
}

static void
bcdef_delink(bcdef_t * bcd)
{
	bcexp_delink(bcd->exp);
}

/*
 * ---------------------------------------------------------------------- 
 */

/*
 * expects a spec of the form {...}{...}:.... 
 */

static spocp_result_t
bcond_eval(element_t * qp, element_t * rp, bcspec_t * bcond, octet_t * oct)
{
	octet_t         spec;
	element_t      *tmp, *xp = 0;
	int             sl, rl;
	spocp_result_t	rc = SPOCP_DENIED;
	cmd_param_t     cpt;

	octln(&spec, bcond->spec);

	while (spec.len && *spec.val == '{') {
		spec.len--;
		spec.val++;
		if ((sl = octchr(&spec, '}')) < 0)
			break;

		rl = spec.len - (sl + 1);
		spec.len = sl;

		if ((tmp = element_eval(&spec, qp, &rc)) == NULL) {
			tmp = element_new();
			tmp->type = SPOC_NULL;
		}
		xp = element_list_add(xp, tmp);

		spec.val += sl + 1;
		spec.len = rl;
	}

	if (*spec.val != ':')
		return SPOCP_SYNTAXERROR;
	spec.val++;
	spec.len--;

	cpt.q = qp;
	cpt.r = rp;
	cpt.x = xp;
	cpt.arg = &spec;
	cpt.pd = bcond->plugin->dyn;
	cpt.conf = bcond->plugin->conf;

	rc = bcond->plugin->test(&cpt, oct);

	if (xp)
		element_free(xp);

	return rc;
}

/*
 * ---------------------------------------------------------------------- 
 */

/*!
 * \brief Evaluates a boundary expression
 * \param qp The element representation of the query
 * \param rp The element representation of the matched rule 
 * \param bce The boundary expression
 * \param oa Place for the dynamic blobs
 * \return A spocp result code
 */
spocp_result_t
bcexp_eval(element_t * qp, element_t * rp, bcexp_t * bce, octarr_t ** oa)
{
	int             n, i;
	spocp_result_t  r = SPOCP_DENIED;
	octet_t         oct;
	octarr_t       *lo = 0, *po = 0;

	oct.size = oct.len = 0;

	switch (bce->type) {
	case SPEC:
		r = bcond_eval(qp, rp, bce->val.spec, &oct);
		if (r == SPOCP_SUCCESS && oct.len) {
			lo = octarr_add(0, octdup(&oct));
			octclr(&oct);
		}
		break;

	case NOT:
		r = bcexp_eval(qp, rp, bce->val.single, &lo);
		if (r == SPOCP_SUCCESS) {
			if (lo)
				octarr_free(lo);
			r = SPOCP_DENIED;
		} else if (r == SPOCP_DENIED)
			r = SPOCP_SUCCESS;
		break;

	case AND:
		n = varr_len(bce->val.arr);
		r = SPOCP_SUCCESS;
		for (i = 0; r == SPOCP_SUCCESS && i < n; i++) {
			r = bcexp_eval(qp, rp,
				       (bcexp_t *) varr_nth(bce->val.arr, i),
				       &po);
			if (r == SPOCP_SUCCESS && po)
				lo = octarr_join(lo, po);
		}
		if (r != SPOCP_SUCCESS)
			octarr_free(lo);
		break;

	case OR:
		n = varr_len(bce->val.arr);
		r = SPOCP_DENIED;
		for (i = 0; r != SPOCP_SUCCESS && i < n; i++) {
			r = bcexp_eval(qp, rp,
				       (bcexp_t *) varr_nth(bce->val.arr, i),
				       &lo);
		}
		break;

	case REF:
		r = bcexp_eval(qp, rp, bce->val.ref->exp, &lo);
		break;
	}

	if (r == SPOCP_SUCCESS && lo)
		*oa = octarr_join(*oa, lo);

	return r;
}

/*
 * If it's a reference to a boundary condition it should be of the form (ref
 * <name>) 
 */
static spocp_result_t
is_bcref(octet_t * o, octet_t * res)
{
	octet_t         lc;
	octet_t         op;
	spocp_result_t  r;

	if (*o->val == '(') {

		octln(&lc, o);
		lc.val++;
		lc.len--;

		if ((r = get_str(&lc, &op)) != SPOCP_SUCCESS)
			return r;
		if (oct2strcmp(&op, "ref") != 0)
			return SPOCP_SYNTAXERROR;

		if ((r = get_str(&lc, &op)) != SPOCP_SUCCESS)
			return r;
		if (!(*lc.val == ')' && lc.len == 1))
			return SPOCP_SYNTAXERROR;

		octln(res, &op);

		return SPOCP_SUCCESS;
	}

	return SPOCP_SYNTAXERROR;
}

static char    *
bcname_make(octet_t * name)
{
	size_t          len;
	char           *str;

	len = name->len + 6 + 1;
	str = (char *) Malloc(len);
	/*
	 * sprintf( str, "BCOND:%s", name->val ) ; 
	 */
	strcat(str, "BCOND:%s");
	strncpy(str + 6, name->val, name->len);
	str[6 + name->len] = '\0';

	return str;
}

/*
 * ---------------------------------------------------------------------- *
 * PUBLIC INTERFACES *
 * ---------------------------------------------------------------------- 
 */

/*! Is this a proper specification of a boundary condition ? \param spec A
 * pointer to the boundary condition specification \return TRUE is true or
 * FALSE if not 
 */
int
bcspec_is(octet_t * spec)
{
	int             n;

	if ((n = octchr(spec, ':')) < 0)
		return FALSE;

	for (n--; n >= 0 && DIRCHAR(spec->val[n]); n--);

	/*
	 * should I check the XPath definitions ? 
	 */

	if (n == -1)
		return TRUE;
	else
		return FALSE;
}

/*! \brief Adds a boundary condition definition to the list of others 
 * \param db A link to the present set of boundary conditions 
 * \param p Pointer to the linked list of registered plugins 
 * \param dbc Command parameters connected to the persistent store 
 * \param name The name of the boundary condition specification 
 * \param data The boundary condition specification 
 * \return A pointer to the internal representation of the boundary condition
 */
bcdef_t        *
bcdef_add(bcdef_t **rbc, plugin_t *p, dbcmd_t *dbc, octet_t *name,octet_t *data)
{
	bcexp_t        *bce;
	bcdef_t        *bcd, *bc;
	char            cname[45], *bcname;
	stree_t        *st;
	octet_t         tmp;

	if (*data->val == '(') {
		if ((st = parse_bcexp(data)) == 0)
			return 0;
	} else {
		st = (stree_t *) Calloc(1,sizeof(stree_t));
		octcpy(&st->val, data);
	}

	bcd = bcdef_new();

	bce = transv_stree(p, st, *rbc, bcd);

	stree_free(st);

	if (bce == 0) {
		bcdef_free(bcd);
		return 0;
	}

	if (bce->type != REF) {
		if (name == 0 || name->len == 0) {
			make_name(cname, data);
			bcd->name = Strdup(cname);
		} else {
			bcd->name = oct2strdup(name, 0);
		}
	}

	/* If there is a persistent store */
	if (dbc && dbc->dback) {
		/*
		 * create a bcond name that MUST differ from rule ids Since
		 * rule ids are SHA1 hashes, it consists of lower case letters 
		 * a-f and numbers. So making the bcond name in the persistent 
		 * store start with "BCOND:" will make it absolutely different. 
		 */

		oct_assign(&tmp, bcd->name);
		bcname = bcname_make(&tmp);
		dback_save(dbc, bcname, data, 0, 0);
		Free(bcname);
	}

	bcd->exp = bce;

	if (bcd->name) {
		if (*rbc == 0)
			*rbc = bcd;
		else {
			for (bc = *rbc; bc->next; bc = bc->next);
			bc->next = bcd;
			bcd->prev = bc;
		}
	}

	return bcd;
}

/*
 * ---------------------------------------------------------------------- 
 */
/*! \brief Remove a boundary condition from the internal database. A
 * boundary condition can not be removed if there is rules that uses it!
 * \param db A pointer to the internal database 
 * \param dbc Command parameters connected to the persistent store 
 * \param name The name of the boundary condition 
 * \return A result code, SPOCP_SUCCESS on success 
 */
spocp_result_t
bcdef_del(bcdef_t **rbc , dbcmd_t * dbc, octet_t * name)
{
	bcdef_t        *bcd;
	char           *bcname;

	bcd = bcdef_find(*rbc, name);

	if (bcd->users && varr_len(bcd->users) > 0)
		return SPOCP_UNWILLING;
	if (bcd->rules && varr_len(bcd->rules) > 0)
		return SPOCP_UNWILLING;


	if (dbc && dbc->dback){
		bcname = bcname_make(name);
		dback_delete(dbc, bcname);
		Free(bcname);
	}

	/*
	 * this boundary conditions might have links to other links
	 * which should then also be removed 
	 */
	bcdef_delink(bcd);

	bcdef_rm(*rbc, bcd);

	bcdef_free(bcd);

	return SPOCP_SUCCESS;
}

/*
 * ---------------------------------------------------------------------- 
 */
/*! \brief Replaces on boundary condition with another without changing the
 * name. 
 * \param db A pointer to the internal database 
 * \param p Pointer to the set of plugins that is present 
 * \param dbc Command parameters connected to the persistent store 
 * \param name The name of the boundary condition 
 * \param data The new specification for the boundary condition 
 * \return An appropriate result code 
 */
spocp_result_t
bcdef_replace(bcdef_t **rbc, plugin_t * p, dbcmd_t * dbc, octet_t * name,
	      octet_t * data)
{
	bcdef_t        *bcd;
	bcexp_t        *bce;
	stree_t        *st;
	char           *bcname;

	if (*data->val == '(') {
		if ((st = parse_bcexp(data)) == 0)
			return SPOCP_OPERATIONSERROR;
	} else {
		st = (stree_t *) Malloc(sizeof(stree_t));
		st->list = 0;
		st->next = st->part = 0;
		st->val.size = 0;	/* otherwise octcpy might core dump */
		octcpy(&st->val, data);
	}

	if ((bcd = bcdef_find(*rbc, name)) != 0) {

		bce = transv_stree(p, st, *rbc, bcd);


		if (dbc && dbc->dback){
			bcname = bcname_make(name);
			dback_replace(dbc, bcname, data, 0, 0);
			Free( bcname);
		}

		bcexp_free(bcd->exp);

		bcd->exp = bce;
	} else
		stree_free(st);

	return SPOCP_SUCCESS;
}

/*
 * ---------------------------------------------------------------------- 
 */
/*! \brief Given a boundary condition specification, return a pointer to the
 * internal representation. That includes returning a pointer to a already
 * existing stored boundary condition if this specification is a reference.
 * Or if the specification is not a simple reference, store it and return a
 * pointer to where it is stored. If the specification is equal to the string
 * "NULL" then a NULL pointer is returned.
 * \param db A pointer to the internal database
 * \param o The boundary conditions specification
 * \param p Pointer to the set of plugins that is present
 * \param dbc Command parameters connected to the persistent store
 * \param rc Where the result code of the * operation should be stored
 * \return A pointer to the internal representation 
 * or NULL if it does not exist, is faulty or references unknown boundary
 * conditions.  If the boundary condition specification is equal to "NULL",
 * NULL is also returned but together with the result code SPOCP_SUCCESS. 
 */
bcdef_t        *
bcdef_get(bcdef_t **rbc, plugin_t * p, dbcmd_t * dbc, octet_t * o,
	  spocp_result_t * rc)
{
	bcdef_t        *bcd = 0;
	spocp_result_t  r;
	octet_t         br;

	if (oct2strcmp(o, "NULL") == 0)
		bcd = NULL;
	else if ((r = is_bcref(o, &br)) == SPOCP_SUCCESS) {
		bcd = bcdef_find(*rbc, &br);
		if (bcd == NULL)
			*rc = SPOCP_UNAVAILABLE;
	} else {
		bcd = bcdef_add(rbc, p, dbc, 0, o);
		if (bcd == 0)
			*rc = SPOCP_SYNTAXERROR;
	}

	return bcd;
}


/*
 * ---------------------------------------------------------------------- 
 */
/*! \brief Checks whether a boundary condition is true or false. If the
 * backend that does the evaluation return true it might be accompanied by a
 * dynamic blob.
 * \param ep A pointer to a internal representation of the parsed query S-expression
 * \param id A collection of a set of rules that are '<=' the query.
 * \param oa An octarr struct. this is where the dynamic blob
 * will be placed if the backend wants to return one.
 * \return A result code 
 */

spocp_result_t
bcond_check(element_t * ep, spocp_index_t * id, octarr_t ** oa, checked_t **cr)
{
	int             i;
	spocp_result_t  r = SPOCP_DENIED;
	ruleinst_t	*ri = 0;
	checked_t	*c;
	octarr_t	*loa=0;
	octet_t		*blob=0;

	/*
	 * find the top 
	 */
	while (ep->memberof)
		ep = ep->memberof;

	DEBUG(SPOCP_DSTORE)
		traceLog(LOG_INFO, "%d rules to check", id->n);
#ifdef AVLUS
	index_print( id );
#endif

	for (i = 0; i < id->n; i++) {
		ri = id->arr[i];

		if ((c = checked_rule( ri, cr ))) {
#ifdef AVLUS
			checked_print(c);
#endif
			r = c->rc;
			if (c->blob)
				*oa = octarr_add(*oa, octdup(c->blob));
			if (r == SPOCP_SUCCESS)
				break;
		}

		if (ri->bcond == 0) {
#ifdef AVLUS
			traceLog(LOG_INFO, "No bcond");
#endif
			if (ri->blob)
#ifdef AVLUS
				traceLog(LOG_INFO, "Adds blob");
#endif
				blob = octdup(ri->blob);
			r = SPOCP_SUCCESS;
		}
		else {
			r = bcexp_eval(ep, ri->ep, ri->bcond->exp, &loa);
			if (loa) {
				blob = loa->arr[0];
				*oa = octarr_add(*oa, blob);
				loa->n = 0;
				octarr_free(loa);
			}
			traceLog(LOG_INFO,
				"boundary condition \"%s\" returned %d\n",
				 ri->bcond->name, r);
		}

		if (r == SPOCP_SUCCESS) {
			add_checked( ri, r, octdup(blob), cr );
			if (blob)
				*oa = octarr_add(*oa, blob);
			
			LOG(SPOCP_INFO) {
				oct_print(LOG_INFO,"Matched rule",ri->rule);
			}
			break;
		}
	}
#ifdef AVLUS
	traceLog(LOG_INFO,"Blob list");
	octarr_print( LOG_INFO, *oa );
	index_print( id );
#endif


	return r;
}

/*
 * ---------------------------------------------------------------------- 
 */
/*! \brief A niffty function that allows you to find all rules that are
 * dependent on a specific boundary condition. 
 * \param db A pointer to the internal database 
 * \param bcname The name of the boundary condition 
 * \return A array of pointers to the rules that uses, directly of indirectly, this
 * boundary condition 
 */
varr_t         *
bcond_users(bcdef_t *rbc, octet_t * bcname)
{
	bcdef_t        *bcd;

	bcd = bcdef_find(rbc, bcname);

	return all_rule_users(bcd, 0);
}

/*
 * ---------------------------------------------------------------------- 
 */
