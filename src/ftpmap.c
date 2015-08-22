/*  ftpmap.c - the FTP-Map project
 
  Copyright 2001-2015 The FTP-Map developers.

*/
/*
  FTP-Map is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  FTP-Map is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ftpmap.h"
#include "testcmds.h"
#include "fingerprints.h"
#include "exploits.h"
#include "versions.h"
#include "tcp.h"
#include "misc.h"
#include "logger.h"
#include "client.h"

#define DEBUG   1

void ftpmap_init(ftpmap_t *ftpmap) {
    ftpmap->port   = strdup(FTP_DEFAULT_PORT);
    ftpmap->user = strdup(FTP_DEFAULT_USER);
    ftpmap->password = strdup(FTP_DEFAULT_PASSWORD);
    ftpmap->nolog = 1;
}

void ftpmap_end(ftpmap_t *ftpmap, detect_t *detect, exploit_t *exploit) {
    fprintf(ftpmap->fid, "QUIT\r\n");
    free(ftpmap);
    free(detect);
    free(exploit);
}

void print_version(int c) {
        printf("Copyright FTP-Map %s (c) 2015 FTP-Map developers.\n"
                "\n-=[ Compiled in: %s %s\n"
                "-=[ Bug reports/help to: %s\n",VERSION, 
                __DATE__, __TIME__, PACKAGE_BUGREPORT);

        exit(c);
}
void print_usage(int ex) {
    printf("Usage: ftpmap -s [host] [OPTIONS]...\n\n"
          "Options:\n"
          "\t--scan, -S                 - Start FTP scan on a single target.\n"
          "\t--server, -s <host>        - FTP server.\n"
          "\t--list, -L <file>          - Scan ftp servers from a list.\n"
          "\t--port, -P <port>          - The FTP port (default: 21).\n"
          "\t--user, -u <user>          - FTP user (default: anonymous).\n"
          "\t--password, -p <password>  - FTP password (default: NULL). \n"
          "\t--execute, -x <cmd>        - Run command on the FTP server.\n"
          "\t--nofingerprint, -n        - Do not generate fingerprint.\n"
          "\t--force, -f                - Force to generate fingerprint.\n"
          "\t--output, -o <file>        - output file for the log file..\n"
          "\t--log, -g                  - Create log file.\n"
          "\t--list, -l <path>          - Get list of files and folders on the FTP server.\n"
          "\t--delete <path>            - Delete files/folders on the server.\n"
          "\t--last-modified, -m <file> - Returns the last-modified time of the given file.\n"
          "\t--size, -z <file>          - Get file size on the remote server\n."
          "\t--download, -d <file>      - Download a file from the FTP Server.\n"
          "\t--upload, -U <file>        - Upload a file to the FTP Server.\n"
          "\t--cat, -c  <file>          - Print remote file (like cat).\n"
         "\n\nGeneral Options:\n"
          "\t--version, -v              - Show version information and quit.\n"
          "\t--help, -h                 - Show help and quit.\n"
          "\nPlease send bug reports/help to "PACKAGE_BUGREPORT
          "\nLicense GPLv2: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>.\n");
    exit(ex);
}

void print_startup(ftpmap_t *ftpmap) {
    /*printf("%s", ftpmap_ascii);*/
    logger_write(1,ftpmap, "\n:: Starting FTP-Map %s - Scanning (%s:%s)...\n", VERSION, ftpmap->ip_addr, ftpmap->port);
}

char * ftpmap_getanswer(ftpmap_t *ftpmap) {
    static char answer[MAX_ANSWER];
    char buffer[MAX_STR];
    char *s = NULL;
    signal(SIGALRM, sigalrm);
    alarm(5);

    if ( !ftpmap->fid )
        die(1, "Cannont read data\n");

    while ((fgets(answer, sizeof(answer), ftpmap->fid)) > 0 ) {
        if (strtol(answer, &s, 10) != 0 && s != NULL) {
            if (isspace(*s)) {
                return answer;
            }
        }
    }
    if (*answer == 0) {        
        ftpmap_reconnect(ftpmap,1);
    }

    return answer;
}

char * ftpmap_getanswer_long(FILE *fd, ftpmap_t *ftpmap) {
    static char ret[MAX_ANSWER];
    char answer[MAX_ANSWER];

    signal(SIGALRM, sigalrm);
    alarm(5);
 
    if ( ! ftpmap->fid )
        die(1, "cannot to read data.");
    while (fgets(answer, sizeof answer, fd) != NULL) {
        strncat(ret, answer, strlen(answer));  
    }
    if (*answer == 0) {        
        ftpmap_reconnect(ftpmap,1);
    }

    return ret;
}


void ftpmap_detect_version_by_banner(ftpmap_t *ftpmap, detect_t *detect) {
    const char **ptr = NULL;

    logger_write(1,ftpmap,":: Trying to detect FTP server by banner...\n");
    sprintf(ftpmap->unsoftware, "%s%s", detect->software, detect->version);

    for ( ptr = versions; *ptr; ptr++ ) {
        if ( ! strcasecmp(ftpmap->unsoftware, *ptr)) {
            logger_write(1,ftpmap,":> FTP server running: %s\n", *ptr);
            logger_write(1,ftpmap,":: No need to generate fingerprint. (use -F to disable this)\n");
            ftpmap->versiondetected = 1;
            ftpmap->skipfingerprint = 1;
            break;
        }
        else
            ftpmap->versiondetected = 0;
    }
}

int ftpmap_login(ftpmap_t *ftpmap, detect_t *detect, int v) {
    char *answer = NULL;
    answer = ftpmap_getanswer(ftpmap);
    if ( v )
        logger_write(1,ftpmap,":: FTP Banner: %s", answer);
    sscanf(answer, "220 %s %s", detect->software, detect->version);
    if ( v )
        logger_write(1,ftpmap,":: Trying to login with: %s:%s\n", ftpmap->user, ftpmap->password);

    fprintf(ftpmap->fid, "USER %s\r\n",ftpmap->user);
    answer = ftpmap_getanswer(ftpmap);

    if ( answer == 0 )
        ftpmap_reconnect(ftpmap,1);

    if ( *answer == '2' )
        return 0;
    
    fprintf(ftpmap->fid, "PASS %s\r\n",ftpmap->password);
    answer = ftpmap_getanswer(ftpmap);  

    if ( answer == 0 )
        ftpmap_reconnect(ftpmap,1);

    if ( *answer == '2' ) {
        if ( v ) {
            logger_write(1,ftpmap,":: %s", answer);
        }
        else {
            printf(":: %s", answer); 
        }
        return 0;
    }

    if ( v ) {
        logger_write(1,ftpmap,":: %s", answer);
    }
    else if ( v ){
        printf(":: %s", answer); 
    }
    return -1;
}

void ftpmap_findexploit(ftpmap_t *ftpmap, detect_t *detect, exploit_t *exploit) {
    const char **ptr = NULL;
    int cexploit = 0;
    int state = 0;

    if ( detect->fisoftware )
        sscanf(detect->fisoftware, "%s %s", detect->fsoftware, detect->fversion);

    logger_write(1,ftpmap,":: Searching exploits...\n");

    state = ftpmap->versiondetected ? ftpmap->versiondetected : ftpmap->fingerprinthasmatch;

    for ( ptr = exploits; *ptr; ptr++ ) {
        sscanf(*ptr, "%d %[^\n]s", &exploit->id, exploit->exploit);
        switch(state) {
            case 1:
                if ( strcasestr(exploit->exploit, detect->software) && strcasestr(exploit->exploit, detect->version)) {
                    ftpmap_draw_extable(ftpmap,exploit->id, exploit->exploit);
                    cexploit++;
                }
                break;
            case 2:
                if ( strcasestr(exploit->exploit, detect->software) && strcasestr(exploit->exploit, detect->version)) {
                    ftpmap_draw_extable(ftpmap,exploit->id, exploit->exploit);
                    cexploit++;
                }
                break;
            case 0:
                if ( strcasestr(exploit->exploit, detect->software) && strcasestr(exploit->exploit, detect->version)) {
                    if ( atoi(detect->version) == 0 || atof(detect->version) == 0 ) {
                        printf(":: FTP detected version is not float or int.\n");
                        return;
                    }
                    ftpmap_draw_extable(ftpmap,exploit->id, exploit->exploit);
                    cexploit++;
                }
                break;
        }
    }

    if ( cexploit == 0 )
        logger_write(1,ftpmap,":: FTP-Map didn't find any exploits for %s\n", ftpmap->server);
    else
        logger_write(1,ftpmap,":: FTP-Map found: %d exploits for %s\n", cexploit, ftpmap->server);
}

int ftpmap_updatestats(const unsigned long sum, int testnb) {
    FP *f = fingerprints;
    int nf = sizeof fingerprints / sizeof fingerprints[0];
    long long err;
    
    do {
        err = (signed long long) f->testcase[testnb] - (signed long long) sum;
        if (err < 0LL) {
            err = -err;
        }
        if (err > 0LL) {
            f->err += (unsigned long) err;
        }
        f++;
        nf--;
    } while (nf != 0);
    return 0;
}

const char * seqidx2difficultystr(const unsigned long long idx) {
    return  (idx < 100ULL)? "Trivial joke" : (idx < 1000ULL)? "Easy" : (idx < 4000ULL)? "Medium" : (idx < 8000ULL)? "Formidable" : (idx < 16000ULL)? "Worthy challenge" : "Good luck!";
}

int ftpmap_findseq(ftpmap_t *ftpmap) {
    char *answer;
    int a, b, c, d, e, f;
    unsigned int port[5];
    unsigned int rndports[10000];
    int n = 0;
    unsigned long long dif = 0ULL;
    long portdif;
    int timedep = 0;
            
    srand(time(NULL));
    do {
        rndports[n] = 1024 + 
            (int) ((1.0 * (65536 - 1024) * rand()) / (RAND_MAX + 1.0));
        n++;
    } while (n < (sizeof rndports / sizeof rndports[0]));
    
    n = 0;
    do {
        fprintf(ftpmap->fid, "PASV\r\n");
        answer = ftpmap_getanswer(ftpmap);
        if (*answer != '2') {
            noseq:                        
            logger_write(1,ftpmap,":: Unable to determine FTP port sequence numbers\n");
            return -1;
        }
        while (*answer != 0 && *answer != '(') {
            answer++;
        }
        if (*answer != '(') {
            goto noseq;
        }
        answer++;    
        if (sscanf(answer, "%u,%u,%u,%u,%u,%u", &a, &b, &c, &d, &e, &f) < 6) {
            goto noseq;
        }
        port[n] = e * 256U + f;
        n++;
    } while (n < (sizeof port / sizeof port[0]));
    logger_write(1,ftpmap,":: FTP port sequence numbers : \n");
    n = 0;
    do {
        logger_write(1,ftpmap,":: %u, ", port[n]);
        if (n != 0) {
            portdif = (long) port[n] - (long) port[n - 1];
            if (portdif < 0L) {
                portdif = -portdif;
            }
            dif += (unsigned long long) portdif;        
        }
        {
            int n2 = 0;
            
            do {
                if (rndports[n2] == port[n]) {
                    timedep++;
                    break;
                }
                n2++;
            } while (n2 < (sizeof rndports / sizeof rndports[0]));
        }        
        n++;
    } while (n < (sizeof port / sizeof port[0]));
    if (timedep > 2) {
        logger_write(1,ftpmap,"\t::: POSSIBLE TRIVIAL TIME DEPENDENCY - INSECURE :::\n");
    }
    dif /= (sizeof port / sizeof port[0] - 1);
    logger_write(1,ftpmap,"\n:: Difficulty = %llu (%s)\n", dif, seqidx2difficultystr(dif));
    return 0;
}

int ftpmap_compar(const void *a_, const void *b_) {
    const FP *a = (const FP *) a_;
    const FP *b = (const FP *) b_;
    
    if (a->err != b->err) {
        return a->err - b->err;
    }
    return strcasecmp(b->software, a->software);
}

int ftpmap_findwinner(ftpmap_t *ftpmap, detect_t *detect) {
    FP *f = fingerprints;
    int nb = sizeof fingerprints / sizeof fingerprints[0];
    int nrep = 0;
    double max,maxerr;
    const char *olds = NULL;

    logger_write(1,ftpmap,":: This may be running :\n\n");
    qsort(fingerprints, sizeof fingerprints / sizeof fingerprints[0],
          sizeof fingerprints[0], ftpmap_compar);
    maxerr = (double) fingerprints[nb - 1].err;
    do {        
        max = ((double) f->err * 100.0) / maxerr;

        if (olds == NULL || strcasecmp(olds, f->software) != 0) {
            olds = f->software;
            ftpmap_draw(0x2d, 30);
            logger_write(1,ftpmap,"%d) %s - %.2g%%\n", nrep+1, f->software, max);
            nrep++;            
        }
        if ( nrep == 1 )
            sprintf(detect->fisoftware, "%s", f->software);
        if ( max <= 0.40 && nrep == 1 )
            ftpmap->fingerprinthasmatch = 2;
        if (nrep > 2) {
            break;
        }
        f++;
        nb--;
    } while (nb != 0);
    ftpmap_draw(0x2d, 30);
    putchar(0x0a);
    return 0;    
}

unsigned long ftpmap_checksum(const char *s) {
    unsigned long checksum = 0UL;

    while (*s != 0) {
        checksum += (unsigned char) *s++;
    }
    return checksum;
}

int ftpmap_fingerprint(ftpmap_t *ftpmap, detect_t *detect) {
    char *answer = NULL;
    const char **cmd;
    unsigned long sum;
    int testnb = 0, progress = 0, max = 0;

    logger_write(1,ftpmap,":: Trying to detect FTP server by fingerprint...\n");
    cmd = testcmds;
    max = 141;

    logger_write(0, ftpmap, "\n# Fingerprint:\n\n");
    logger_write(0, ftpmap, "{\n\t0UL, \"%s %s\",{\n", detect->software, detect->version);
    while (*cmd != NULL) {
        fprintf(ftpmap->fid, "%s\r\n", *cmd);
        fflush(ftpmap->fid);
        answer = ftpmap_getanswer(ftpmap);
        if (answer == NULL) {
            sum = 0UL;
        } else {
            sum = ftpmap_checksum(answer);
        }

        printf(":: Generating fingerprint [%d%%]\r", progress * 100 / max );
        fflush(stdout);
        logger_write(0, ftpmap, "%lu,", sum);
        ftpmap_updatestats(sum, testnb);
        testnb++;                    
        cmd++;
        progress++;
    }
    logger_write(0, ftpmap, "\n\t}\n},\n");
    putchar(0x0a);
    return 0;
}

void ftpmap_sendcmd(ftpmap_t *ftpmap) {
    char *answer = NULL;
    const char **lptr = NULL;
    int shorto = 1;

    logger_write(1,ftpmap,":: Sending cmd: %s.\n", ftpmap->cmd);
    fprintf(ftpmap->fid, "%s\r\n", ftpmap->cmd);

    for ( lptr = long_output_cmds; *lptr; lptr++ ) {
        if ( strcasecmp(*lptr, ftpmap->cmd) == 0 ) {
           logger_write(1,ftpmap,"::: Retrieving data for %s...\n\n", ftpmap->cmd);
            answer = ftpmap_getanswer_long(ftpmap->fid, ftpmap);
            shorto = 0;
            break;
        }

    }
    if ( shorto )
        answer = ftpmap_getanswer(ftpmap);
    logger_write(1,ftpmap,"%s", answer);
}

void ftpmap_calc_data_port(ftpmap_t *ftpmap) {
    char *answer = NULL, *actualstr = NULL;
    char str[MAX_STR];
    int h1 = 0, h2 = 0, h3 = 0, h4 = 0, p1 = 0, p2 = 0;

    /* You must call this function after ftpmap_login() */
    fprintf(ftpmap->fid, "PASV\r\n");
    answer = ftpmap_getanswer(ftpmap);
   
    /* Not logged in or worng command*/
    if ( *answer == '5' ) {
        logger_write(1,ftpmap,"%s", answer);
        return;
    }
    
    sprintf(str, "%s", (actualstr = strstr(answer, "(")));
    sscanf(str, " (%d,%d,%d,%d,%d,%d)", &h1,&h2,&h3,&h4,&p1,&p2);
    /*h1.h2.h3.h4 - the server IP address*/
    ftpmap->dataport = p1*256+p2;
}

void ftpmap_get_systemtype(ftpmap_t *ftpmap) {
    char *answer = NULL;

    fprintf(ftpmap->fid, "SYST\r\n");
    answer = ftpmap_getanswer(ftpmap);

    if ( *answer == '5' ) 
        logger_write(1,ftpmap, ":: SYST command failed.\n");
    else
        logger_write(1,ftpmap, ":> System type (OS) : %s", answer+4);
}

void ftpmap_scanlist(ftpmap_t *ftpmap, detect_t *detect, exploit_t *exploit) {
    char buffer[MAX_STR];
    FILE *fp;

    if (( fp = fopen(ftpmap->path, "r")) == NULL )
        die(1, "Failed to read: %s\n", ftpmap->path);

    while ( fgets(buffer, sizeof(buffer), fp)) {
        if ( buffer ) {
            ftpmap->server = strtok(buffer, "\n");
            ftpmap_scan(ftpmap, detect, exploit, 1);
        }
        bzero(buffer, 0);
    }

    fclose(fp);
}

void ftpmap_scan(ftpmap_t *ftpmap, detect_t *detect, exploit_t *exploit, int override) {
    ftpmap_reconnect(ftpmap, 0);
    logger_open(ftpmap, override); 
    print_startup(ftpmap);
    ftpmap_login(ftpmap, detect, 1);
    ftpmap_get_systemtype(ftpmap);
    ftpmap_detect_version_by_banner(ftpmap,detect);
    if ( ftpmap->skipfingerprint == 0 || ftpmap->forcefingerprint ) {
        ftpmap_fingerprint(ftpmap, detect);
        ftpmap_findwinner(ftpmap,detect);
    }
    ftpmap_findexploit(ftpmap,detect,exploit);
    ftpmap_findseq(ftpmap);
    printf("\n:: Scan for: %s completed ::\n", ftpmap->ip_addr);
    printf(":: Please send the fingerprint to hypsurus@mail.ru to improve FTP-Map.\n\n");  
    if ( ftpmap->nolog == 0 )
        logger_close(ftpmap);
}

int main(int argc, char **argv) {
    int opt = 0, long_index = 0;
    int action = 0;
    ftpmap_t *ftpmap = xmalloc(sizeof (*ftpmap));
    detect_t *detect = xmalloc(sizeof (*detect));
    exploit_t *exploit = xmalloc(sizeof (*exploit));

    ftpmap_init(ftpmap);

    static struct option long_options[] = {
        {"server", required_argument, 0, 's'},
        {"scan", no_argument, 0, 'S'},
        {"nolog", no_argument, 0, 'g'},
        {"port",   required_argument, 0, 'P'},
        {"user",   required_argument, 0, 'u'},
        {"password", required_argument, 0, 'p'},
        {"execute", required_argument, 0, 'x'},
        {"download", required_argument, 0, 'd'},
        {"upload", required_argument, 0, 'U'},
        {"last-modified", required_argument, 0, 'm'},
        {"force", no_argument, 0, 'f'},
        {"output", required_argument, 0, 'o'},
        {"nofingerprint", no_argument, 0, 'n'},
        {"list", required_argument, 0, 'l'},
        {"delete", required_argument, 0, 'D'},
        {"log", no_argument, 0, 'g'},
        {"size", required_argument, 0, 'z'},
        {"cat", required_argument, 0, 'c'},
        {"list", required_argument, 0, 'L'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
    };

    while (( opt = getopt_long(argc, argv, "s:P:u:p:x:fl:hvo:D:m:Sd:U:gz:c:L:n", 
                    long_options, &long_index)) != -1 ) {
            misc_check(optarg);
            switch(opt) {
                case 's':
                        ftpmap->server = optarg;
                        break;
                case 'S':
                        action = 10;
                        break;
                case 'P':
                        ftpmap->port = strdup(optarg);
                        break;
                case 'u':
                        ftpmap->user = strdup(optarg);
                        break;
                case 'p':
                        ftpmap->password = strdup(optarg);
                        break;
                case 'z':
                        action = 7;
                        ftpmap->path = strdup(optarg);
                        break;
                case 'x':
                        ftpmap->cmd = strdup(optarg);
                        action = 5;
                        break;
                case 'f':
                        ftpmap->forcefingerprint = 1;
                        break;
                case 'o':
                        ftpmap->loggerfile = strdup(optarg);
                        break;
                case 'n':
                        ftpmap->skipfingerprint = 1;
                        break;
                case 'l':
                        ftpmap->path = strdup(optarg);
                        action = 1;
                        break;
                case 'D':
                        ftpmap->path = strdup(optarg); 
                        action = 2;
                        break;
                case 'm':
                        ftpmap->path = strdup(optarg);
                        action = 3;
                        break;
                case 'd':
                        ftpmap->path = strdup(optarg);
                        action = 4;
                        break;
                case 'U':
                        ftpmap->path = strdup(optarg);
                        action = 6;
                        break;
                case 'c':
                        ftpmap->path = strdup(optarg);
                        action = 8;
                        break;
                case 'g':
                        ftpmap->nolog = 0;
                        break;
                case 'L':
                        ftpmap->path = strdup(optarg);
                        action = 9;
                        break;
                case 'h':
                        print_usage(0);
                case 'v':
                        print_version(0);
                default:
                        print_usage(0);
             }
        }
    
    if ( ftpmap->server == NULL && action <= 8 ) {
        printf("Error: Please tell me what server has to be probed (-s <host>)\n\n");
        print_usage(1);
    }
    else if ( ftpmap->server && action == 9 )
        die(1,"Error: you cannont use -s with -L\n");

    else if ( action <= 8 ) {
        ftpmap_reconnect(ftpmap, 0);
        logger_open(ftpmap, 0);
        ftpmap_login(ftpmap, detect, 0);
    } 

    switch(action) {
        case 1:
            ftpmap_getlist(ftpmap);
            goto end;
        case 2:
            ftpmap_delete(ftpmap);
            goto end;
        case 3:
            ftpmap_mdtm(ftpmap);
            goto end;
        case 4:
            ftpmap_download(ftpmap);
            goto end;
        case 5:
            ftpmap_sendcmd(ftpmap);
            goto end;
        case 6:
            ftpmap_upload(ftpmap);
            goto end;
        case 7:
            logger_write(1,ftpmap, "%s\n", calc_bytes_size(ftpmap_fsize(ftpmap)));
            goto end;
        case 8:
            ftpmap_cat(ftpmap);
            goto end;
        case 9:
            ftpmap_scanlist(ftpmap, detect, exploit);
            goto end;
        case 10:
            ftpmap_scan(ftpmap, detect, exploit, 0);
            goto end;
            
    }

   end:
        ftpmap_end(ftpmap, detect, exploit);
    
    return 0;
}

