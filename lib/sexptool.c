#include <sexptool.h>
#include <string.h>
#include <func.h>
#include <wrappers.h>

/*
 * format is typically (3:foo%{var}) which is then split into three parts
 * "(3:foo", %{var} and ")" var is looked up in the table of known variables
 * and if it exists the function connected to that variable is included in the 
 * list 
 */

sexparg_t  **
parse_format(const char *format, const sexparg_t transf[], int ntransf )
{
	sexparg_t         **arg;
	char           *sp, *vp, *tp;
	int             i = 0, j = 0, n;
	char           *copy;

	copy = Strdup((char *) format);

	/*
	 * This is the max size I know that for sure 
	 */
	n = (ntransf * 2) + 2;

	arg = (sexparg_t **) Calloc(n, sizeof(sexparg_t *));

	for (sp = copy; *sp;) {
		if ((vp = strstr(sp, "%{"))) {
			vp += 2;
			tp = find_balancing(vp, '{', '}');
			if (tp == 0) {	/* format error or not, your guess is
					 * as good as mine */
				break;	/* I guess it's a string */
			} else {
				if (vp - 2 != sp) {
					*(vp - 2) = '\0';
					arg[j] =
					    (sexparg_t *) malloc(sizeof(sexparg_t));
					arg[j]->var = strdup(sp);
					arg[j]->af = 0;
					arg[j]->type = 'l';
					arg[j]->dynamic = TRUE;
					j++;
					*(vp - 2) = '$';
				}

				*tp = '\0';
				for (i = 0; i < ntransf; i++) {
					/*
					 * traceLog( "(%d)[%s] == [%s] ?", i,
					 * transf[i].var, vp ) ; 
					 */
					if (strcmp(transf[i].var, vp) == 0) {
						arg[j] =
						    (sexparg_t *)
						    malloc(sizeof(sexparg_t));
						arg[j]->var = 0;
						arg[j]->af = transf[i].af;
						arg[j]->type = transf[i].type;
						arg[j]->dynamic =
						    transf[i].dynamic;
						j++;
						break;
					}
				}

				/*
				 * unknown variable, clear out and close down 
				 */
				if (i == ntransf) {
					traceLog
					    ("parse_format: variable [%s] unknown",
					     vp);
					for (i = 0; arg[i] && i < n; i++)
						free(arg[i]);
					free(arg);
					return 0;
				}
			}

			sp = tp + 1;
		} else
			break;
	}

	if (*sp) {		/* trailing string, which is quite ok */
		arg[j] = (sexparg_t *) malloc(sizeof(sexparg_t));
		arg[j]->var = strdup(sp);
		arg[j]->af = 0;
		arg[j]->type = 'l';
	}

	return arg;
}

char    *
sexp_constr( void *comarg, sexparg_t ** ap )
{
	int             i;
	unsigned int    size = 2048;
	char           *argv[32], format[32];
	char            sexp[2048], *res;

	if (ap == 0)
		return 0;

	for (i = 0; ap[i]; i++) {
		format[i] = ap[i]->type;

		if (ap[i]->var)
			argv[i] = ap[i]->var;
		else
			argv[i] = ap[i]->af(comarg);

		if (0)
			LOG(SPOCP_DEBUG) traceLog("SEXP argv[%d]: [%s]", i,
						  argv[i]);
	}
	format[i] = '\0';
	argv[i] = 0;

	if (0)
		LOG(SPOCP_DEBUG) traceLog("SEXP Format: [%s]", format);

	if (sexp_printa(sexp, &size, format, (void **) argv) == 0)
		res = 0;
	else
		res = Strdup(sexp);

	/*
	 * only free them temporary stuff 
	 */
	for (i = 0; ap[i]; i++) {
		if (ap[i]->af && ap[i]->dynamic == TRUE)
			free(argv[i]);
	}

	return res;
}

void sexparg_free( sexparg_t **sa)
{
	int i;

	if (sa) {
		for ( i = 0; sa[i]; i++ ) {
			if (sa[i]->var) free(sa[i]->var);
			free( sa[i] );
		}
		free( sa ) ;
	}
}

