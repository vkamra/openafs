#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <netdb.h>

#include <afsconfig.h>
#include <afs/param.h>
#include <afs/afsint.h>
#include <sys/ioctl.h>
#include <afs/venus.h>
#include <afs/cellconfig.h>
#include <afs/afs.h>
#include <afs/afsutil.h>

/*#include <rx/rxkad.h>*/
#include <rx/rx_null.h>

/*#include <krb.h>*/
#include <afs/com_err.h>

#ifdef AFS_RXK5
#include <rx/rxk5.h>
#include <rx/rxk5errors.h>
#include <afs/rxk5_utilafs.h>
#endif

struct VenusFid {
    afs_int32 Cell;
    struct AFSFid Fid;
};

int
statfile(char *path, char *cellname, afs_uint32 * server, struct AFSFid *f)
{

    struct ViceIoctl v;
    struct VenusFid vf;
    afs_int32 srvbuf[MAXHOSTS];
    int code;

    if (!strncmp(path, "@afs:", 5)) {
	char *pdup, *p, *host, *id;
	struct hostent *he;

	pdup = strdup(path);
	strtok(pdup, ":");

	if (!(p = strtok(NULL, ":"))) {
	    free(pdup);
	    return -1;
	}
	strncpy(cellname, p, MAXCELLCHARS);

	if (!(p = strtok(NULL, ":"))) {
	    free(pdup);
	    return -1;
	}
	he = gethostbyname(p);
	if (!he) {
	    printf("Unknown host %s\n", p);
	    free(pdup);
	    return -1;
	}
	memcpy(server, he->h_addr, he->h_length);

	if (!(p = strtok(NULL, ":"))) {
	    free(pdup);
	    return -1;
	}
	f->Volume = atoi(p);

	if (!(p = strtok(NULL, ":"))) {
	    free(pdup);
	    return -1;
	}
	f->Vnode = atoi(p);

	if (!(p = strtok(NULL, ":"))) {
	    free(pdup);
	    return -1;
	}
	f->Unique = atoi(p);

	if (strtok(NULL, ":")) {
	    printf("Too much extra stuff after @afs:...\n");
	    free(pdup);
	    return -1;
	}

	return 0;
    }

    v.in = 0;
    v.in_size = 0;
    v.out = cellname;
    v.out_size = MAXCELLCHARS;
    if ((code = pioctl(path, VIOC_FILE_CELL_NAME, &v, 1)))
	return code;

    v.out = (char *)&vf;
    v.out_size = sizeof(struct VenusFid);
    if ((code = pioctl(path, VIOCGETFID, &v, 1)))
	return code;
    memcpy(f, &vf.Fid, sizeof(struct AFSFid));

    v.out = (char *)srvbuf;
    v.out_size = sizeof(srvbuf);
    if ((code = pioctl(path, VIOCWHEREIS, &v, 1)))
	return code;
    if (v.out_size <= sizeof(afs_int32))
	return EINVAL;

    memcpy(server, srvbuf, sizeof(afs_int32));
    return 0;
}


extern int RXAFSCB_ExecuteRequest();
struct rx_securityClass *sc;

extern int
start_cb_server()
{
    struct rx_service *s;

    sc = rxnull_NewServerSecurityObject();
    s = rx_NewService(0, 1, "afs", &sc, 1, RXAFSCB_ExecuteRequest);
    if (!s)
	return 1;
    rx_StartServer(0);
    return 0;
}


/*extern int rx_socket;*/
extern unsigned short rx_port;
int
do_rx_Init(void)
{
    struct sockaddr_in s;
    int len;

    if (rx_Init(0)) {
	fprintf(stderr, "Cannot initialize rx\n");
	return 1;
    }

    len = sizeof(struct sockaddr_in);
    if (getsockname(rx_socket, &s, &len)) {
	perror("getsockname");
	return 1;
    }
    rx_port = ntohs(s.sin_port);

    return 0;
}

struct rx_securityClass *
get_sc(char *cellname, int security_class)
{
#if 0
    char realm[REALM_SZ];
    CREDENTIALS c;
#endif

#ifdef AFS_RXK5
int code;
char* rxk5_keytab;
krb5_creds *k5_creds, in_creds[1];
krb5_context k5context;
krb5_ccache cc;

#else
    return rxnull_NewClientSecurityObject();
#endif

    if(security_class == 5) {
	struct afsconf_dir *tdir;
	struct afsconf_cell info[1];
	char *afs_k5_princ;
    
        code = krb5_init_context(&k5context);
    	if(code) {
	    printf("Error krb5_init_context - %d (%s)\n", code, error_message(code));
	    exit(1);
    	}
	
        memset(&cc, 0, sizeof cc);
        code = krb5_cc_default(k5context, &cc);
        if(code) {
	    printf("Error krb5_cc_default - %d (%s)\n", code, error_message(code));
	    exit(1);
    	}
	
    	memset(in_creds, 0, sizeof *in_creds);
    	code = krb5_cc_get_principal(k5context, cc, &in_creds->client);
    	if(code) {
	    printf("Error krb5_cc_get_principal - %d (%s)\n", code, error_message(code));
	    exit(1);
    	}
	tdir = afsconf_Open(AFSDIR_CLIENT_ETC_DIRPATH);
	if (!tdir) {
	    printf ("Problem with <%s>?\n", AFSDIR_CLIENT_ETC_DIRPATH);
	    exit(1);
	}
	code = afsconf_GetCellInfo(tdir, cellname, 0, info);
	if (code) {
	    printf ("Error afsconf_GetCellInfo - %d (%s)\n", code, error_message(code));
	    exit(1);
	}
    	afs_k5_princ = get_afs_krb5_svc_princ(info);
	afsconf_Close(tdir);
	if (!afs_k5_princ) {
	    printf ("get_afs_krb5_svc_princ failed (no memory?)\n");
	    exit(1);
	}
    	code = krb5_parse_name(k5context, afs_k5_princ,	&in_creds->server);
    	if(code) {
	    printf("Error krb5_parse_name - %d (%s)\n", code, error_message(code));
	    exit(1);
    	}
	free(afs_k5_princ);		
						
    	// 0 is cc flags
    	code = krb5_get_credentials(k5context, 0, cc, in_creds, &k5_creds);
    	if(code) {
	    printf("Error krb5_get_credentials - %d (%s)\n", code, error_message(code));
	    exit(1);
    	}
	return rxk5_NewClientSecurityObject(rxk5_crypt, k5_creds, 0);


    } else {
    	return rxnull_NewClientSecurityObject();
    }

#if 0

    ucstring(realm, cellname, REALM_SZ);

    if (krb_get_cred("afs", "", realm, &c)) {
	if (get_ad_tkt("afs", "", realm, DEFAULT_TKT_LIFE)) {
	    return NULL;
	} else {
	    if (krb_get_cred("afs", "", realm, &c)) {
		return NULL;
	    }
	}
    }

    return rxkad_NewClientSecurityObject(rxkad_clear, c.session, c.kvno,
					 c.ticket_st.length, c.ticket_st.dat);
#endif
}

#define scindex_NULL 0
#define scindex_RXKAD 2
#ifdef AFS_RXK5
#define scindex_RXK5 5
#define scindex scindex_RXK5
#else
#define scindex scindex_RXKAD
#endif

int
main(int argc, char **argv)
{
    char scell[MAXCELLCHARS], dcell[MAXCELLCHARS];
    afs_uint32 ssrv, dsrv;
    char *databuffer, *srcf, *destd, *destf, *destpath;
    struct stat statbuf;

    struct AFSStoreStatus sst;
    struct AFSFetchStatus fst, dfst;
    struct AFSVolSync vs;
    struct AFSCallBack scb, dcb;
    struct AFSFid sf, dd, df;

    int filesz;
    int ch, blksize, bytesremaining, bytes;
    struct timeval start, finish;
    struct timezone tz;
    struct rx_securityClass *ssc = 0, *dsc = 0;
    int sscindex, dscindex;
    struct rx_connection *sconn, *dconn;
    struct rx_call *scall, *dcall;
    int code, fetchcode, storecode, printcallerrs;
    int slcl = 0, dlcl = 0;
    int sfd, dfd, unauth = 0;
#ifdef AFS_RXK5
    int rxk5 = 0;
#endif

    struct AFSCBFids theFids;
    struct AFSCBs theCBs;


    blksize = 8 * 1024;
#ifdef AFS_RXK5
    rxk5 = env_afs_rxk5_default() != FORCE_RXKAD;
#endif

#ifdef AFS_RXK5
#define RXK5_OPT_STRING "k45"
#else
#define RXK5_OPT_STRING /**/
#endif
    while ((ch = getopt(argc, argv, "io" RXK5_OPT_STRING "ub:")) != -1) {
	switch (ch) {
	case 'b':
	    blksize = atoi(optarg);
	    break;
	case 'i':
	    slcl = 1;
	    break;
	case 'o':
	    dlcl = 1;
	    break;
	case 'u':
	    unauth = 1;
	    break;
#ifdef AFS_RXK5
	case '4':
	    rxk5 = 0;
	    break;
	case 'k':
	case '5':
	    rxk5 = 1;
	    break;
#endif
	default:
	    printf("Unknown option '%c'\n", ch);
	    exit(1);
	}
    }


    if (argc - optind < 2) {
	fprintf(stderr,
		"Usage: afscp [-i|-o]] [-b xfersz] [-u] "
#ifdef AFS_RXK5
		"[-k5] [-k4] "
#endif
		"source dest\n");
	fprintf(stderr, "  -b   Set block size\n");
	fprintf(stderr, "  -i   Source is local (copy into AFS)\n");
	fprintf(stderr, "  -o   Dest is local (copy out of AFS)\n");
	fprintf(stderr, "  -u   Run unauthenticated\n");
#ifdef AFS_RXK5
	fprintf(stderr, "  -k5  Use rxk5\n");
	fprintf(stderr, "  -k4  Use rxkad\n");
#endif
	fprintf(stderr, "source and dest can be paths or specified as:\n");
	fprintf(stderr, "     @afs:cellname:servername:volume:vnode:uniq\n");
	exit(1);
    }
    srcf = argv[optind++];
    destpath = argv[optind++];
    destd = strdup(destpath);
    if (!destd) {
	perror("strdup");
	exit(1);
    }
    if ((destf = strrchr(destd, '/'))) {
	*destf++ = 0;
    } else {
	destf = destd;
	destd = ".";
    }


    if (!slcl && statfile(srcf, scell, &ssrv, &sf)) {
	fprintf(stderr, "Cannot get attributes of %s\n", srcf);
	exit(1);
    }
    if (!dlcl && statfile(destd, dcell, &dsrv, &dd)) {
	fprintf(stderr, "Cannot get attributes of %s\n", destd);
	exit(1);
    }

    if ((databuffer = malloc(blksize)) == NULL) {
	perror("malloc");
	exit(1);
    }

    if (do_rx_Init())
	exit(1);

    if (start_cb_server()) {
	printf("Cannot start callback service\n");
	goto Fail_rx;
    }

    if (!slcl) {
    
#ifdef AFS_RXK5
	if(rxk5) {
	    sscindex = scindex_RXK5;
	} else
#endif
	sscindex = scindex_RXKAD;
	if (unauth || (ssc = get_sc(scell, sscindex)) == NULL) {
	    ssc = rxnull_NewClientSecurityObject();
	    sscindex = scindex_NULL;
	    /*printf("Cannot get authentication for cell %s; running unauthenticated\n", scell); */
	}
	
	if ((sconn =
	     rx_NewConnection(ssrv, htons(AFSCONF_FILEPORT), 1, ssc,
			      sscindex))
	    == NULL) {
	    struct in_addr s;
	    s.s_addr = ssrv;
	    printf("Cannot initialize rx connection to source server (%s)\n",
		   inet_ntoa(s));
	    goto Fail_sc;
	}
    }

    if (!dlcl) {
	if (!slcl && ssrv == dsrv) {
	    dconn = sconn;
	    dsc = NULL;
	} else {
	    if (slcl || strcmp(scell, dcell)) {
#ifdef AFS_RXK5
		if(rxk5) {
	    	    dscindex = scindex_RXK5;
		} else
#endif
		dscindex = scindex_RXKAD;
		if (unauth || (dsc = get_sc(dcell, sscindex)) == NULL) {
		    dsc = rxnull_NewClientSecurityObject();
		    dscindex = scindex_NULL;
		    /*printf("Cannot get authentication for cell %s; running unauthenticated\n", dcell); */
		}
	    } else {
		dsc = ssc;
		dscindex = sscindex;
	    }
	    		    
	    if ((dconn =
		 rx_NewConnection(dsrv, htons(AFSCONF_FILEPORT), 1, dsc,
				  dscindex))
		== NULL) {
		struct in_addr s;
		s.s_addr = dsrv;
		printf
		    ("Cannot initialize rx connection to dest server (%s)\n",
		     inet_ntoa(s));
		goto Fail_sconn;
	    }
	}
    }


    memset(&sst, 0, sizeof(struct AFSStoreStatus));

    if (dlcl) {
	dfd = open(destpath, O_RDWR | O_CREAT | O_EXCL, 0666);
	if (dfd < 0 && errno == EEXIST) {
	    printf("%s already exists, overwriting\n", destpath);
	    dfd = open(destpath, O_RDWR | O_TRUNC, 0666);
	    if (dfd < 0) {
		fprintf(stderr, "Cannot open %s (%s)\n", destpath,
			error_message(errno));
		goto Fail_dconn;
	    }
	} else if (dfd < 0) {
	    fprintf(stderr, "Cannot open %s (%s)\n", destpath,
		    error_message(errno));
	    goto Fail_dconn;
	}
    } else {
	if ((code =
	     RXAFS_CreateFile(dconn, &dd, destf, &sst, &df, &fst, &dfst, &dcb,
			      &vs))) {
	    if (code == EEXIST) {
		printf("%s already exits, overwriting\n", destpath);
		if (statfile(destpath, dcell, &dsrv, &df))
		    fprintf(stderr, "Cannot get attributes of %s\n",
			    destpath);
		else
		    code = 0;
	    } else {
		printf("Cannot create %s (%s)\n", destpath,
		       error_message(code));
		if (code)
		    goto Fail_dconn;
	    }
	}
    }

    if (slcl) {
	sfd = open(srcf, O_RDONLY, 0);
	if (sfd < 0) {
	    fprintf(stderr, "Cannot open %s (%s)\n", srcf,
		    error_message(errno));
	    goto Fail_dconn;
	}
	if (fstat(sfd, &statbuf) < 0) {
	    fprintf(stderr, "Cannot stat %s (%s)\n", srcf,
		    error_message(errno));
	    close(sfd);
	    goto Fail_dconn;
	}
    } else {
	if ((code = RXAFS_FetchStatus(sconn, &sf, &fst, &scb, &vs))) {
	    printf("Cannot fetchstatus of %d.%d (%s)\n", sf.Volume, sf.Vnode,
		   error_message(code));
	    goto Fail_dconn;
	}
    }



    if (slcl) {
	filesz = statbuf.st_size;
    } else {
	filesz = fst.Length;
    }

    printcallerrs = 0;
    fetchcode = 0;
    storecode = 0;
    if (!slcl)
	scall = rx_NewCall(sconn);
    if (!dlcl)
	dcall = rx_NewCall(dconn);
    gettimeofday(&start, &tz);

    if (!slcl) {
	if ((code = StartRXAFS_FetchData(scall, &sf, 0, filesz))) {
	    printf("Unable to fetch data from %s (%s)\n", srcf,
		   error_message(code));
	    goto Fail_call;
	}
    }

    if (!dlcl) {
	if (slcl) {
	    sst.Mask = AFS_SETMODTIME | AFS_SETMODE;
	    sst.ClientModTime = statbuf.st_mtime;
	    sst.UnixModeBits =
		statbuf.st_mode & ~(S_IFMT | S_ISUID | S_ISGID);
	} else {
	    sst.Mask = AFS_SETMODTIME | AFS_SETMODE;
	    sst.ClientModTime = fst.ClientModTime;
	    sst.UnixModeBits =
		fst.UnixModeBits & ~(S_IFMT | S_ISUID | S_ISGID);
	}

	if ((code =
	     StartRXAFS_StoreData(dcall, &df, &sst, 0, filesz, filesz))) {
	    printf("Unable to store data to %s (%s)\n", destpath,
		   error_message(code));
	    goto Fail_call;
	}
    }

    if (slcl) {
	bytesremaining = statbuf.st_size;
    } else {
	rx_Read(scall, &bytesremaining, sizeof(afs_int32));
	bytesremaining = ntohl(bytesremaining);
    }

    while (bytesremaining > 0) {
	/*printf("%d bytes remaining\n",bytesremaining); */
	if (slcl) {
	    if ((bytes =
		 read(sfd, databuffer, min(blksize, bytesremaining))) <= 0) {
		fetchcode = errno;
		break;
	    }
	} else {
	    if ((bytes =
		 rx_Read(scall, databuffer,
			 min(blksize, bytesremaining))) <= 0)
		break;
	}
	if (dlcl) {
	    if (write(dfd, databuffer, bytes) != bytes) {
		storecode = errno;
		break;
	    }
	} else {
	    if (rx_Write(dcall, databuffer, bytes) != bytes)
		break;
	}
	bytesremaining -= bytes;
	/*printf("%d bytes copied\n",bytes); */
    }


    if (bytesremaining > 0) {
	printf("Some network error occured while copying data\n");
	goto Fail_call;
    }

    if (!slcl)
	fetchcode = EndRXAFS_FetchData(scall, &fst, &scb, &vs);
    if (!dlcl)
	storecode = EndRXAFS_StoreData(dcall, &fst, &vs);
    printcallerrs = 1;
  Fail_call:

    if (slcl) {
	if (close(sfd) && !fetchcode)
	    fetchcode = errno;
    } else {
	fetchcode = rx_EndCall(scall, fetchcode);
    }
    if (fetchcode && printcallerrs)
	printf("Error returned from fetch: %s\n", error_message(fetchcode));

    if (dlcl) {
	if (close(dfd) && !storecode)
	    storecode = errno;
    } else {
	storecode = rx_EndCall(dcall, storecode);
    }
    if (storecode && printcallerrs)
	printf("Error returned from store: %s\n", error_message(storecode));

    gettimeofday(&finish, &tz);

    if (!slcl) {
	theFids.AFSCBFids_len = 1;
	theFids.AFSCBFids_val = &sf;
	theCBs.AFSCBs_len = 1;
	theCBs.AFSCBs_val = &scb;
	scb.CallBackType = CB_DROPPED;
	if ((code = RXAFS_GiveUpCallBacks(sconn, &theFids, &theCBs)))
	    printf("Could not give up source callback: %s\n",
		   error_message(code));
    }

    if (!dlcl) {
	theFids.AFSCBFids_len = 1;
	theFids.AFSCBFids_val = &df;
	theCBs.AFSCBs_len = 1;
	theCBs.AFSCBs_val = &dcb;
	dcb.CallBackType = CB_DROPPED;
	if ((code = RXAFS_GiveUpCallBacks(dconn, &theFids, &theCBs)))
	    printf("Could not give up target callback: %s\n",
		   error_message(code));
    }

    if (code == 0)
	code = storecode;
    if (code == 0)
	code = fetchcode;

  Fail_dconn:
    if (!dlcl && (slcl || dconn != sconn))
	rx_DestroyConnection(dconn);
  Fail_sconn:
    if (!slcl)
	rx_DestroyConnection(sconn);
  Fail_sc:
    if (dsc && dsc != ssc)
	RXS_Close(dsc);
    if (ssc)
	RXS_Close(ssc);
  Fail_rx:
    rx_Finalize();

    free(databuffer);
    if (printcallerrs) {
	double rate, size, time;
	if (finish.tv_sec == start.tv_sec) {
	    printf("Copied %d bytes in %d microseconds\n", filesz,
		   finish.tv_usec - start.tv_usec);
	} else {
	    printf("Copied %d bytes in %d seconds\n", filesz,
		   finish.tv_sec - start.tv_sec);
	}

	size = filesz / 1024.0;
	time =
	    finish.tv_sec - start.tv_sec + (finish.tv_usec -
					    start.tv_usec) / 1e+06;
	rate = size / time;
	printf("Transfer rate %g Kbytes/sec\n", rate);

    }

    exit(code != 0);
}
