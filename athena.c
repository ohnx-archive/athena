#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>

#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define INFO       43
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404

//Configuration
char *nick = "athenabot";
char *channel = "##ohnx";
char *host = "holmes.freenode.net";
char *port = "6667";
char *cmdprefix = ",";
char *realname = "athena bot";
char *ownernick = "ohnx";
char *logdir = "irclogs/";
char *partmsg = "You're gonna miss me when I'm gone";
//SHA512 password hash so you don't have to store plaintext, online please enter the unhashed password - if you don't want to use sha512 and change it, please change client side as well!
char *password = "";
int webport = 25568;

struct {
        char *ext;
        char *filetype;
} extensions [] = {
        {"gif", "image/gif" },
        {"jpg", "image/jpeg"},
        {"jpeg","image/jpeg"},
        {"png", "image/png" },
        {"zip", "image/zip" },
        {"gz",  "image/gz"  },
        {"tar", "image/tar" },
        {"htm", "text/html" },
        {"html","text/html" },
        {"log","text/plain" },
        {"css","text/css" },
        {"js","text/javascript" },
        {"ico","image/x-icon"},
        {0,0}
};

//PIDs for stuff
int ircpid=0;
int webpid=0;

void sighandle(int signo) {
	if (signo == SIGUSR1) {
		(void)signal(SIGCLD, SIG_IGN); /* ignore child death */
		ircpid=fork();
		if (ircpid==0) {
        	ircmain();
			exit(0);
    	}
	} else if (signo == SIGUSR2) {
		kill(ircpid, SIGTERM);
		ircpid=0;
	}
}

//strcat
char* concat(char *s1, char *s2) {
    char *result = malloc(strlen(s1)+strlen(s2)+1);//+1 for the zero-terminator
    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

//fix channel names
char* fix_name(char s1[]) {
    char* s = malloc(strlen(s1));
    int j = 0;
    while (s1[j] != '\0'){
        if (s1[j]<'!') {
            *(s+j)=(char) 0;
            return s;
        } else if (s1[j] == '#' || s1[j] == ' ') {
            *(s+j)='_';
        } else {
            *(s+j)=s1[j];
        }
        j++;
    }
    return s;
}

//passwords & authentication
char *stopstr;
char *quitstr;
char *startstr;
char *passwordstr;
void initpasswordstr() {
	startstr = malloc(sizeof(char)*(strlen(password)+12));
	strcpy(startstr, "GET /");
	strcat(startstr, password);
	strcat(startstr, "/start\0");
	stopstr = malloc(sizeof(char)*(strlen(password)+11));
	strcpy(stopstr, "GET /");
	strcat(stopstr, password);
	strcat(stopstr, "/stop\0");
	quitstr = malloc(sizeof(char)*(strlen(password)+11));
	strcpy(quitstr, "GET /");
	strcat(quitstr, password);
	strcat(quitstr, "/quit\0");
	passwordstr = malloc(sizeof(char)*(strlen(password)+6));
	strcpy(passwordstr, "GET /");
	strcat(passwordstr, password);
	strcat(passwordstr, "\0");
}
static int *validip;

//public logger information
void logger(int type, char *s1, char *s2, int socket_fd) {
	int fd;
	char logbuffer[BUFSIZE*2];
	switch (type) {
	case ERROR: (void)sprintf(logbuffer,"[ERROR] %s:%s Errno=%d exiting pid=%d\n",s1, s2, errno,getpid());
		break;
	case FORBIDDEN:
		(void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
		(void)sprintf(logbuffer,"[FORBIDDEN] %s:%s\n",s1, s2);
		break;
	case NOTFOUND:
		(void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
		(void)sprintf(logbuffer,"[NOT FOUND] %s:%s\n",s1, s2);
		break;
	case INFO: (void)sprintf(logbuffer," INFO: %s:%s:%d\n",s1, s2,socket_fd); break;
	case LOG: /*(void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); */break;
	}
	/* No checks here, nothing can be done with a failure anyway */
	if ((fd = open("web.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
		(void)write(fd,logbuffer,strlen(logbuffer));
		(void)close(fd);
	}
	if (type == ERROR || type == NOTFOUND || type == FORBIDDEN) exit(3);
}

//Child web handler
void web(int fd, int hit) {
	int j, file_fd, buflen;
	long i, ret, len;
	char * fstr;
	static char buffer[BUFSIZE+1]; /* static so zero filled */

	ret =read(fd,buffer,BUFSIZE); 	/* read Web request in one go */
	if (ret == 0 || ret == -1) {	/* read failure stop now */
		logger(FORBIDDEN,"failed to read browser request","",fd);
	}
	if (ret > 0 && ret < BUFSIZE)	/* return code is valid chars */
		buffer[ret]=0;		/* terminate the buffer */
	else buffer[0]=0;
	for(i=0;i<ret;i++)	/* remove CF and LF characters */
		if (buffer[i] == '\r' || buffer[i] == '\n')
			buffer[i]='*';
	logger(LOG,"request",buffer,hit);
	if ( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
		logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
	}
	for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
		if (buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
			buffer[i] = 0;
			break;
		}
	}
	for(j=0;j<i-1;j++) 	/* check for illegal parent directory use .. */
		if (buffer[j] == '.' && buffer[j+1] == '.') {
			logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
		}

	//Custom functions
	if (!strncmp(&buffer[0],quitstr,strlen(quitstr))) {
		logger(LOG,"Going down!","",0);
		kill(ircpid, SIGKILL);
		kill(0, SIGKILL);
		exit(1);
	} else if (!strncmp(&buffer[0],"GET /list\0",10)) {
		logger(LOG,"Listing files...","",0);
		static char lfiles[BUFSIZE+1]; /* static so zero filled */
		strcat(lfiles, "<ul>");
		DIR *d;
		struct dirent *dir;
		d = opendir(".");
		if (d) {
			while ((dir = readdir(d)) != NULL) {
				char *dot = strrchr(dir->d_name, '.');
				if (dot && !strcmp(dot, ".log")) {
					strcat(lfiles, "<li><a class=\"loglink\" href=\"#\" id=\"");
					strcat(lfiles, dir->d_name);
					strcat(lfiles, "\">");
					strcat(lfiles, dir->d_name);
					strcat(lfiles, "</a></li>\n");
				}
			}
			closedir(d);
		}
		strcat(lfiles, "</ul>");
		len = strlen(lfiles);
		(void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: text/html\n\n", VERSION, len); /* Header + a blank line */
		(void)write(fd, buffer, strlen(buffer));
		(void)write(fd, lfiles, len);
		sleep(1);	/* allow socket to drain before signalling the socket is closed */
		close(fd);
		exit(1);
	} else if (!strncmp(&buffer[0],stopstr,strlen(stopstr))) {
		logger(LOG,"Killing IRC bot!","",ircpid);
		kill(webpid, SIGUSR2);
		exit(1);
	} else if (!strncmp(&buffer[0],startstr,strlen(startstr))) {
		logger(LOG,"Starting IRC bot!","",0);
		kill(webpid, SIGUSR1);//Raise SIGUSR1
		exit(1);
	} else if (!strncmp(&buffer[0],"GET /status\0",12)) {
		logger(LOG,"Bot status","",0);
		static char result[8]; /* static so zero filled */
		if(ircpid==0)
			strcat(result, "botdown");
		else
			strcat(result, "botup");
		(void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: text/html\n\n%s", VERSION, strlen(result), result); /* Header + a blank line */
		(void)write(fd, buffer, strlen(buffer));
		sleep(1);	/* allow socket to drain before signalling the socket is closed */
		close(fd);
		exit(1);
	} else if (!strncmp(&buffer[0],passwordstr,strlen(passwordstr))) {
		//getpeername google it
		//someone has successfully logged in
		(void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: 3\nConnection: close\nContent-Type: text/html\n\nyes", VERSION);//nope
		(void)write(fd, buffer, strlen(buffer));
		sleep(1);	/* allow socket to drain before signalling the socket is closed */
		close(fd);
		exit(1);
	}
	
	if ( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
		(void)strcpy(buffer,"GET /index.html");

	/* work out the file type and check we support it */
	buflen=strlen(buffer);
	fstr = (char *)0;
	for(i=0;extensions[i].ext != 0;i++) {
		len = strlen(extensions[i].ext);
		if ( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
			fstr =extensions[i].filetype;
			break;
		}
	}
	if (fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

	if (( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
		logger(NOTFOUND, "failed to open file",&buffer[5],fd);
	}
	//logger(LOG,"SEND",&buffer[5],hit);
	len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
	      (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
          (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nExpires: Thu, 19 Nov 1981 08:52:00 GMT\nCache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\nPragma: no-cache\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */

	(void)write(fd,buffer,strlen(buffer));

	/* send file in 8KB block - last block may be smaller */
	while (	(ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
		(void)write(fd,buffer,ret);
	}
	sleep(1);	/* allow socket to drain before signalling the socket is closed */
	close(fd);
	exit(1);
}

//IRC functions
//IRC portion
int conn;
char sbuf[512];
char minibuff[128];
//internal commands
void raw(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sbuf, 512, fmt, ap);
    va_end(ap);
    write(conn, sbuf, strlen(sbuf));
}
void changenick(char* nick) {
    raw("NICK %s\r\n", nick);
}
void say(char *chan, char *message) {
    struct tm *tm;
    FILE *olog;
    time_t t;
    char timestamp[100];
    t = time(NULL);
    tm = localtime(&t);
    strftime(timestamp, sizeof(timestamp), "[%d/%m/%y](%H:%M:%S)", tm);
    raw("PRIVMSG %s :%s\r\n", chan, message);
    char* s;
    if (sw(chan,"#")) {
		s = concat(fix_name(chan), ".log");
		olog = fopen(s, "a");
		strftime(timestamp, sizeof(timestamp), "[%d/%m/%y](%H:%M:%S)", tm);
		fprintf(olog, "%s %s: %s\n", timestamp, nick, message);//need newline here because there is none
		fflush(olog);
		free(s);
	}
}
void me(char *chan, char *message) {
    raw("PRIVMSG %s :%cACTION %s%c\r\n", chan, 1, message, 1);
}
void join(char *chan) {
    raw("JOIN %s\r\n", chan);
}
void part(char *chan, char *message) {
    raw("PART %s :%s\r\n", chan, message);
}
int sw(const char *a, const char *b) {
   if (strncmp(a, b, strlen(b)) == 0) return 1;
   return 0;
}
char* delPrefix(char *toDel, int num) {
    toDel += num;
    return toDel;
}
char* firstWord(char *line) {
    char *word = strtok(line," ");
    return word;
}
char* delWord(char *toDel) {
    char* newStr = toDel;
    while (*newStr != 0 && *(newStr++) != ' ') {}
    return newStr;
}
char* getTime() {
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo=localtime(&rawtime);
	return asctime(timeinfo);
}

/* Returns an integer in the range [0, n).
 *
 * Uses rand(), and so is affected-by/affects the same seed.
 */
int randint(int n) {
	return rand()%n;
}

//checkip command too big
void demd5(char *check, char *ou);
//Main IRC bot
int ircmain() {
int game = 0, gennum = 0, numtries = 0;
char *gameplayer;
    //Variables used
    char *user, *command, *where, *message, *sep, *target;
    int i, j, l, sl, o = -1, start, wordcount, prefixlen;
    prefixlen = strlen(cmdprefix);
    char buf[513];
    struct addrinfo hints, *res;
    struct tm *tm;
    time_t t;
    char timestamp[100];
    t = time(NULL);
    tm = localtime(&t);
    strftime(timestamp, sizeof(timestamp), "[%d/%m/%y](%H:%M:%S)", tm);
    memset(minibuff, 0, 128);
    FILE *fp;
	FILE *olog;
	char* s;
	fp = fopen("irc.log", "a");
    fprintf(fp, "New Session started at %s\n", timestamp);
    fflush(fp);
    //Connect to server
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo(host, port, &hints, &res);
    conn = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    connect(conn, res->ai_addr, res->ai_addrlen);
    //send username and nick
    raw("USER %s 0 0 :%s\r\n", nick, realname);
    changenick(nick);
    //start reading server lines
    while ((sl = read(conn, sbuf, 512))) {
        for (i = 0; i < sl; i++) {
            o++;
            buf[o] = sbuf[i];
            if ((i > 0 && sbuf[i] == '\n' && sbuf[i - 1] == '\r') || o == 512) {
                buf[o + 1] = '\0';
                l = o;
                o = -1;
                //Don't touch this - ping replies
                if (!strncmp(buf, "PING", 4)) {
                    buf[1] = 'O';
                    raw(buf);
                } else if (buf[0] == ':') {
                    wordcount = 0;
                    user = command = where = message = NULL;
                    for (j = 1; j < l; j++) {
                        if (buf[j] == ' ') {
                            buf[j] = '\0';
                            wordcount++;
                            switch(wordcount) {
                                case 1: user = buf + 1; break;
                                case 2: command = buf + start; break;
                                case 3: where = buf + start; break;
                            }
                            if (j == l - 1) continue;
                            start = j + 1;
                        } else if (buf[j] == ':' && wordcount == 3) {
                            if (j < l - 1) message = buf + j + 1;
                            break;
                        }
                    }
                    //not a real message, dont bother parsing
                    if (wordcount < 2) continue;
                    //comment here
                    if (!strncmp(command, "001", 3) && channel != NULL) {
                        raw("JOIN %s\r\n", channel);
                    } else if (!strncmp(command, "PRIVMSG", 7)) {
                        if (where == NULL || message == NULL) continue;
                        if ((sep = strchr(user, '!')) != NULL) user[sep - user] = '\0';
                        if (where[0] == '#' || where[0] == '&' || where[0] == '+' || where[0] == '!') target = where; else target = user;
                        //printf("[from: %s] [reply-with: %s] [where: %s] [reply-to: %s] %s", user, command, where, target, message);
                        //printf("(%s)%s: %s", where, user, message);
						if(game==1)/* Current game is number guessing */ {
							if(strcmp(user, gameplayer) == 0) {
								if(numtries<=10) /*number was generated*/ {
									int guess = atoi(message);
									if(gennum<guess) say(where, "Too high!");
									else if(gennum>guess) say(where, "Too low!");
									numtries++;
									if(gennum==guess) {memset(minibuff, 0, 128);sprintf(minibuff, "Good job! You guessed the number in %d tries", numtries);say(where, minibuff);game=0;gennum=0;numtries=0;free(gameplayer);gameplayer=NULL;/*Game over!*/}
								} else {memset(minibuff, 0, 128);sprintf(minibuff, "You lost! The number was %d", gennum);say(where, minibuff);game=0;gennum=0;numtries=0;free(gameplayer);gameplayer=NULL;}
							}
						}
						if (sw(message, cmdprefix)) {
							if (!sw(where,"#")) {
								where=user;
							}
							message+=prefixlen;
							if (sw(message, "numguess")){
								if(game==0){
									srand(time(NULL));
									game=1;
									gennum=randint(100);
									say(where, "Starting the game! The number is between 1-100.");
									memset(minibuff, 0, 128);
									sprintf(minibuff, "Say any number you want. You have 10 tries. say %snumguess to quit the game.", cmdprefix);
									say(where, minibuff);
									gameplayer = strdup(user);
								} else {
									if(strcmp(user, gameplayer) == 0) {
										free(gameplayer);
										gameplayer=NULL;
										game=0;
										numtries=0;
										say(where, "Quit the game!");
									} else {
										memset(minibuff, 0, 128);sprintf(minibuff, "Sorry! You can't join right now. %s is already playing.", gameplayer);say(where, minibuff);
									}
								}
							}
							message-=prefixlen;
							//Created new thread for commands
							if(fork()==0) {
			    			//printf("message prefix detected!\n");
			    			message = delPrefix(message, prefixlen);
							size_t ln = strlen(message) - 1;
							message[ln] = '\0';
							if (sw(message, "numguess")){}
			    			else if (sw(message, "nick")){changenick(delPrefix(message, 4));}
			    			else if (sw(message, "cmds")){say(user, "Management: help, join, part, raw, quit.");say(user, "Channel commands: kick, ban, unban, op, voice, deop, devoice.");say(user, "Misc commands: info, time, hug, slap, rr, 8ball, numguess.");}
			    			else if (sw(message, "help gamemode")){say(where, "Game mode is when you do not have to type any prefixes. Anything you say is automatically part of the game.");}
			    			else if (sw(message, "help")){say(where, "Hey there! I'm a simple IRC bot written in C.");say(where, "Want more help? Type ,cmds to see a list of my commands or ,info to get the link to my GitHub page.");}
			    			else if (sw(message, "join")){if (strcmp(user, ownernick) == 0) {join(delPrefix(message, 4));} else say(where, "Nice try...");}
			    			else if (sw(message, "part")){if (strcmp(user, ownernick) == 0) {memset(minibuff, 0, 128);sprintf(minibuff, "PART %s :%s\r\n", delPrefix(message, 4), partmsg);raw(minibuff);/*Clear buffer*/memset(buf, 0, 513);memset(sbuf, 0, 512);} else say(where, "Nice try...");}
			    			else if (sw(message, "raw")){if (strcmp(user, ownernick) == 0) {raw("%s", delPrefix(message, 3));} else say(where, "Nice try...");}
			    			else if (sw(message, "info")){say(where, "My GitHub page: http://masonx.tk/athena");}
			    			else if (sw(message, "time")){say(where, getTime());}
							else if (sw(message, "rr")){srand(time(NULL));int r = rand()%7;memset(minibuff, 0, 128);sprintf(minibuff, "loads the gun and aims it at %s", user);me(where, minibuff);sleep(1);switch(r){case 1:say(where, "The gun fires! :o");break;default:say(where, "The gun clicks. You're safe!");break;}}
							else if (sw(message, "8ball")){srand(time(NULL));int r = rand()%3;switch(r){case 0:say(where, "The chances seem high.");break;case 1:say(where, "I don't know!");break;case 2:say(where, "nope.avi");break;}}
			    			else if (sw(message, "hug")){memset(minibuff, 0, 128);*(message+strlen(message)-1)='\0';sprintf(minibuff, "hugs %s", delPrefix(message, 4));me(where, minibuff);}
			    			else if (sw(message, "slap")){memset(minibuff, 0, 128);*(message+strlen(message)-1)='\0';sprintf(minibuff, "slaps %s", delPrefix(message, 5));me(where, minibuff);}
							else if (sw(message, "unban")){if (strcmp(user, ownernick) == 0) {memset(minibuff, 0, 128);sprintf(minibuff, "MODE %s -b %s\r\n", where, delPrefix(message, 6));raw(minibuff);} else say(where, "Nice try...");}
							else if (sw(message, "ban")){if (strcmp(user, ownernick) == 0) {memset(minibuff, 0, 128);sprintf(minibuff, "MODE %s +b %s\r\n", where, delPrefix(message, 4));raw(minibuff);} else say(where, "Nice try...");}
							else if (sw(message, "kick")){if (strcmp(user, ownernick) == 0) {message = delWord(message);memset(minibuff, 0, 128);sprintf(minibuff, "KICK %s %s :%s\r\n", where, firstWord(message), delWord(message));raw(minibuff); printf("%s", minibuff);} else say(where, "Nice try...");}
							else if (sw(message, "devoice")){if (strcmp(user, ownernick) == 0) {memset(minibuff, 0, 128);sprintf(minibuff, "MODE %s -v %s\r\n", where, delPrefix(message, 8));raw(minibuff);} else say(where, "Nice try...");}
							else if (sw(message, "voice")){if (strcmp(user, ownernick) == 0) {memset(minibuff, 0, 128);sprintf(minibuff, "MODE %s +v %s\r\n", where, delPrefix(message, 6));raw(minibuff);} else say(where, "Nice try...");}
							else if (sw(message, "deop")){if (strcmp(user, ownernick) == 0) {memset(minibuff, 0, 128);sprintf(minibuff, "MODE %s -o %s\r\n", where, delPrefix(message, 5));raw(minibuff);} else say(where, "Nice try...");}
							else if (sw(message, "op")){if (strcmp(user, ownernick) == 0) {memset(minibuff, 0, 128);sprintf(minibuff, "MODE %s +o %s\r\n", where, delPrefix(message, 3));raw(minibuff);} else say(where, "Nice try...");}
			    			else if (sw(message, "quit")){if (strcmp(user, ownernick) == 0) {fclose(fp);/*kill(webpid, SIGKILL);*/kill(webpid, SIGUSR2);} else say(where, "Nice try...");}
							else {say(where, "Unknown command! Type ,cmds for a list of possible commands.");}
							exit(0);
							}
						}
						//raw("%s %s :%s", command, target, message); // If you enable this the IRCd will get its "*** Looking up your hostname..." messages thrown back at it but it works...
						//Log
						//Check if channel
						if (sw(where,"#")) {
							s = concat(fix_name(where), ".log");
							olog = fopen(s, "a");
						    t = time(NULL);
							tm = localtime(&t);
							strftime(timestamp, sizeof(timestamp), "[%d/%m/%y](%H:%M:%S)", tm);
							fprintf(olog, "%s %s: %s", timestamp, user, message);
							fflush(olog);
							free(s);
						} else {
						    t = time(NULL);
							tm = localtime(&t);
						    strftime(timestamp, sizeof(timestamp), "[%d/%m/%y](%H:%M:%S)", tm);
							fprintf(fp, "%s | (%s)%s: %s", timestamp, where, user, message);
							fflush(fp);
						}
						if(strstr(user, "ohnxBotMC") != NULL && strstr(message, "joined the game.") != NULL) {memset(minibuff, 0, 80);sprintf(minibuff, "Hey there, %s!", firstWord(delWord(message)));say(where, minibuff);}
                    } else if (!strncmp(command, "JOIN", 4)) {
                        if(fork()==0) {int num = strlen(buf)+1;
						char *str = buf+num;
						num = strlen(str)+1;
						str = str+num;
						if (sw(str,"#")) {
						    t = time(NULL);
							tm = localtime(&t);
							s = concat(fix_name(str), ".log");
							olog = fopen(s, "a");
							strftime(timestamp, sizeof(timestamp), "[%d/%m/%y](%H:%M:%S)", tm);
							fprintf(olog, "%s %s joined!\n", timestamp, user);
							fflush(olog);
							free(s);
						}
						exit(0);}
					} else if (!strncmp(command, "PART", 4)) {
						if(fork()==0) {//fork so errors wont crash the bot
						int num = strlen(buf)+1;
						char *str = buf+num;
						num = strlen(str)+1;
						str = str+num;
						message[strlen(message)-1] = '\0';
						
						if(message==NULL) {
                                                        s = concat(fix_name(str), ".log");
                                                        olog = fopen(s, "a");
						    t = time(NULL);
							tm = localtime(&t);
                                                        strftime(timestamp, sizeof(timestamp), "[%d/%m/%y](%H:%M:%S)", tm);
                                                        fprintf(olog, "%s %s left\n", timestamp, user);
                                                        fflush(olog);
                                                        free(s);
							exit(0);
						}
						if (sw(str,"#")) {
							s = concat(fix_name(str), ".log");
							olog = fopen(s, "a");
						    t = time(NULL);
							tm = localtime(&t);
							strftime(timestamp, sizeof(timestamp), "[%d/%m/%y](%H:%M:%S)", tm);
							fprintf(olog, "%s %s left (%s)\n", timestamp, user, message);
							fflush(olog);
							free(s);
						}
						exit(0);
						}
                    }
                }
                //closing brackets
            }
        }
        //closing
    }
    fclose(fp);
    //return 1 if the bot dies
    return 1;
}

//Main portion is web portion
int main() {
	if (chdir(logdir) == -1){
		(void)printf("ERROR: Can't Change to directory\n");
		exit(4);
	}
	//init password string
	initpasswordstr();
	//shared memory for allowed ip
	//validip = mmap(NULL, sizeof *validip, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    //*validip = 1;
	//Random seed
	srand(time(NULL));
	if (fork()==0) {
		printf("Athena bot is starting. Prepare for awesomeness.\n");
		//Catch SIGNUSR1, aka "start irc bot"
		if ((signal(SIGUSR1, sighandle) == SIG_ERR)||(signal(SIGUSR2, sighandle) == SIG_ERR)) {
			printf("Error catching signal!");
			exit(6);
		}
		int i, pid, listenfd, socketfd, hit;
		socklen_t length;
		static struct sockaddr_in cli_addr; /* static = initialised to zeros */
		static struct sockaddr_in serv_addr; /* static = initialised to zeros */
		/* Become deamon + unstopable and no zombies children (= no wait()) */
		(void)signal(SIGCLD, SIG_IGN); /* ignore child death */
		(void)setpgrp();		/* break away from process group */

		/* setup the network socket */
		if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) <0)
			logger(ERROR, "system call","socket",0);
		int reuse = 1;
		setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
		if (webport < 0 || webport >60000)
			logger(ERROR,"Invalid port number (try 1->60000)","aww!",0);
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		serv_addr.sin_port = htons(webport);
		if (bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0) {
			perror("Binding the socket failed!");
			exit(0);
		}
		if ( listen(listenfd,64) <0) {
			perror("Listening on the socket failed!");
			exit(0);
		}
		printf("Web started.\n");
		//Fork irc bot
		webpid=getpid();
		ircpid=fork();
		if (ircpid==0) {
			printf("IRC started.\n");
        	ircmain();
    	}
		for(hit=1; ;hit++) {
			length = sizeof(cli_addr);
			if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
				logger(ERROR,"system call","accept",0);
			if ((pid = fork()) < 0) {
				logger(ERROR,"system call","fork",0);
			} else {
				if (pid == 0) { 	/* child */
					(void)close(listenfd);
					web(socketfd,hit); /* never returns */
				} else { 	/* parent */
					(void)close(socketfd);
				}
			}
		}
	}
	sleep(1);
    return 0;
}
