#ifndef __SPOCPCLI__
#define __SPOCPCLI__

#include <unistd.h>
#include <stdio.h>

#ifdef HAVE_SSL
#include <openssl/ssl.h>
#endif

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

/* TLS states */
#define USE	1
#define DEMAND  2
#define VERIFY	4

#include <spocp.h>

typedef struct _spocp_rescode
{
	int code;
	char *num;
} spocp_rescode_t;

enum spocpc_res
{
	SPOCPC_SYNTAXERROR = 1,
	SPOCPC_MISSING_CHAR,
	SPOCPC_OK,
	SPOCPC_SYNTAX_ERROR,
	SPOCPC_PROTOCOL_ERROR,
	SPOCPC_OTHER,
	SPOCPC_TIMEDOUT,
	SPOCPC_CON_ERR,
	SPOCPC_PARAM_ERROR,
	SPOCPC_STATE_VIOLATION,
	SPOCPC_NOSUPPORT
};
/*
typedef struct _octnode
{
	octet_t *oct;
	struct _octnode *next;
} octnode_t;
*/
typedef enum
{
	UNCONNECTED,
	SOCKET,
	SSL_TLS
} spocp_contype_t;

typedef struct _spocp
{
	spocp_contype_t contype;
	int fd;
	char *srv;
	int timeout;		/* timeout in seconds */

#ifdef HAVE_SSL
	/* A couple of cases:
	 * don't demand server certificate
	 * demand server certificate, fail or not depending on whether the verification fails */
	int servercert;		/* whether server cert is demanded or not */
	int verify_ok;		/* TRUE means fail is verify isn't OK */
	int tls;

	SSL_CTX *ctx;
	SSL *ssl;
#endif

	char *sslEntropyFile;
	char *certificate;
	char *privatekey;
	char *calist;
	char *passwd;

} SPOCP;

typedef struct _queres
{
	int		rescode;
	char		*str;
	octarr_t	*blob;
} queres_t;


SPOCP	*spocpc_init(char *srv);
SPOCP	*spocpc_open(SPOCP * spocp, char *srv, int nsec);
int	spocpc_reopen(SPOCP * spocp, int nsec);
ssize_t	spocpc_readn(SPOCP * spocp, char *str, ssize_t max);
ssize_t	spocpc_writen(SPOCP * spocp, char *str, ssize_t max);

int spocpc_parse_and_print_list(char *resp, int n, FILE * fp, int wid);

int spocpc_send_add(SPOCP * spocp, char *path, char *rule, char *bcond,
	char *info, queres_t * qr);
int spocpc_send_subject(SPOCP * spocp, char *subject, queres_t * qr);
int spocpc_send_query(SPOCP *, char *path, char *query, queres_t * qr);
int spocpc_send_delete(SPOCP * spocp, char *path, char *rule, queres_t * qr);
int spocpc_send_logout(SPOCP * spocp);
int spocpc_open_transaction(SPOCP * spocp, queres_t * qr);
int spocpc_commit(SPOCP * spocp, queres_t * qr);
int spocpc_attempt_tls(SPOCP * spocp, queres_t * qr);
int spocpc_start_tls(SPOCP * spocp);

void free_spocp(SPOCP * s);
void spocpc_close(SPOCP * spocp);

/*
octnode_t *spocpc_octnode_new(void);
void spocpc_octnode_free(octnode_t *);
void spocpc_octnode_list_free(octnode_t *);
char *spocpc_oct2strdup(octet_t * op, char ec);
*/

#ifdef HAVE_SSL

void spocpc_tls_use_cert(SPOCP * spocp, char *str);
void spocpc_tls_use_ca(SPOCP * spocp, char *str);
void spocpc_tls_use_calist(SPOCP * spocp, char *str);
void spocpc_tls_use_key(SPOCP * spocp, char *str);
void spocpc_tls_use_passwd(SPOCP * spocp, char *str);
void spocpc_tls_set_demand_server_cert(SPOCP * spocp);
void spocpc_tls_set_verify_server_cert(SPOCP * spocp);

void	spocpc_use_tls(SPOCP * spocp);
int	start_tls(SPOCP * spocp);

#endif

int spocpc_debug;

#define DIGITS(n) ( (n) >= 100000 ? 6 : (n) >= 10000 ? 5 : (n) >= 1000 ? 4 : (n) >= 100 ? 3 : ( (n) >= 10 ? 2 : 1 ) )

#endif
