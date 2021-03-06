#ifndef __SRVCONF_H
#define __SRVCONF_H

typedef enum {
	RULEFILE = 1,
	PORT,
	UNIXDOMAINSOCKET,
	CERTIFICATE,
	PRIVATEKEY,
	CALIST,
	DHFILE,
	ENTROPYFILE,
	PASSWD,
	NTHREADS,
	TIMEOUT,
	LOGGING,
	SSLVERIFYDEPTH,
	PIDFILE,
	MAXCONN
} infotype_t;

#endif
