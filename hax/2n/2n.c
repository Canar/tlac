#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PKG        "2n"
#define VERSION    "0.0.1"
#define RATE       "44100"
#define CHAN       "2"
#define FMT        "f32le"
#define CFGDIR     "/.local/share/" PKG "/"
#define PLAYLISTFN "playlist"
#define STATEFN    "state"

enum ERROR {
	NONE,
	PIPE,
	DECODE_FORK,
	OUTPUT_FORK,
	REFRESH_FORK,
	INPUT_FORK,
	GETTIME,
	PLAYLIST_STAT,
	PLAYLIST_WRITE_OPEN,
	PLAYLIST_WRITE,
	PLAYLIST_WRITE_CLOSE,
	STATE_WRITE_OPEN,
	STATE_WRITE,
	STATE_WRITE_CLOSE,
	PLAYLIST_READ_STAT,
	PLAYLIST_READ_OPEN,
	PLAYLIST_READ,
	PLAYLIST_READ_CLOSE,
	STATE_READ_OPEN,
	STATE_READ,
	STATE_READ_CLOSE,
	STATE_READ_SPRINTF
};

const char* errmsg[]={
	"not an error",
	"pipe()",
	"fork(decode)",
	"fork(output",
	"fork(input)",
	"fork(refresh)",
	"clock_gettime()",
	"stat(playlist)",
	"open(playlist,write)",
	"write(playlist)",
	"close(playlist,write)",
	"open(state,write)",
	"write(state)",
	"close(state,write)",
	"open(playlist,read)",
	"read(playlist)",
	"close(playlist_read)"
	"open(state,read)",
	"read(state)",
	"close(state_read)",
	"sprintf(state_read)"
};

typedef struct state_struct {
	int pos;
	time_t off;
} statest;

void validate(int retval,enum ERROR kind){
	if(retval>=0)
		return;
	printf("ERROR %d: %s failed.\n",kind+1,errmsg[kind]);
	exit((int)kind+1);
}

int prefix_len(int plen,int count,char* ary[]){
	if(count<1)
		return(plen);
	for(int j=0;j<plen;j++)
		for(int i=1;i<count;i++)
			if(!(ary[0][j]==ary[i][j]))
				return(j);
}

int main(int argc, char *argv[]){
	int output_pid;
	int decode_pid=-3; 
	int input_pid=-2;
	int refresh_pid=-1;
	int ret_pid=decode_pid;

	int pipefd[2];
	struct timespec start,now;
	char key;

	int pos=-1; /* position in playlist */
	char** pl;
	char* plmap;
	int plc=argc-1;
	int plen;

	char* home_dir=getenv("HOME");
	if(NULL==home_dir){
		printf("ERROR: $HOME unset. Environment appears faulty.\n");
		goto quit;
	}

	int cfg_dir_len=strlen(home_dir)+strlen(CFGDIR)+1;
	char* cfg_dir=malloc(cfg_dir_len);
	strcpy(cfg_dir,home_dir);
	strcat(cfg_dir,CFGDIR);
	int ret=mkdir(cfg_dir,0775);
	if(-1==ret){
		if(EEXIST!=errno){
			printf("ERROR: %d in mkdir()\n",errno);
			exit(1);
		}
	}

	char* playlist_fn=malloc(cfg_dir_len+strlen(PLAYLISTFN));
	strcpy(playlist_fn,cfg_dir);
	strcat(playlist_fn,PLAYLISTFN);
	char* state_fn=malloc(cfg_dir_len+strlen(STATEFN));
	strcpy(state_fn,cfg_dir);
	strcat(state_fn,STATEFN);

	struct stat st;
	if( ((0>stat(playlist_fn,&st)) && (2>argc)) || (1>st.st_size)){
		printf("ERROR: Playlist is empty. Run " PKG " with a list of files as argument to generate it.\n");
		goto quit;
	}

	int fd;
	int pllen=0;
	/* write arguments to playlist file, enabling run-time mods */
	if(argc>1){
		char** plrp=malloc(plc*sizeof(char*));
		for(int i=0;i<plc;i++){
			plrp[i]=realpath(argv[i+1],NULL);
			pllen+=strlen(plrp[i])+1;
		}

		plmap=malloc(pllen);
		int plmo=strlen(plrp[0])+1;
		memcpy(plmap,plrp[0],plmo);
		for(int i=1;i<plc;i++){
			int plmoi=strlen(plrp[i])+1;
			memcpy(&plmap[plmo],plrp[i],plmoi);
			free(plrp[i]);
			plmo+=plmoi;
		}
		free(plrp);

		validate(fd=open(playlist_fn,O_WRONLY|O_CREAT|O_TRUNC,0775),PLAYLIST_WRITE_OPEN);
		validate(write(fd,plmap,plmo),PLAYLIST_WRITE);
		validate(close(fd),PLAYLIST_WRITE_CLOSE);
	}

	/* load playlist file */
	validate(stat(playlist_fn,&st),PLAYLIST_READ_STAT);
	pllen=st.st_size;
	validate(fd=open(playlist_fn,O_RDONLY),PLAYLIST_READ_OPEN);
	plmap=mmap(NULL,pllen,PROT_READ,MAP_SHARED|MAP_POPULATE,fd,0);
	validate(close(fd),PLAYLIST_READ_CLOSE);
	plc=0;
	for(int i=1;i<pllen;i++)
		if(0==plmap[i])
			plc++;
	pl=malloc(plc*sizeof(char*));
	int j=1;
	pl[0]=&plmap[0];
	for(int i=1;(i<pllen)&&(j<plc);i++)
		if(0==plmap[i])
			pl[j++]=&plmap[i+1];

	/* calculate/print file prefix */
	plen=strlen(pl[0]);
	plen=prefix_len(plen,plc,pl);
	char* prefix=malloc(plen+1);
	memcpy(prefix,pl[0],plen);
	prefix[plen]=0;
	printf("%s\n",prefix);

	/* read state */
	statest state={.off=0,.pos=-1};
	if(!(0>stat(state_fn,&st) || 1<argc)){
		validate(fd=open(state_fn,O_RDONLY),STATE_READ_OPEN);
		validate(read(fd,&state,sizeof(statest))-sizeof(statest),STATE_READ);
		validate(close(fd),STATE_READ_CLOSE);
		pos=state.pos-1;
	}

	close(2);
	validate(pipe(pipefd),PIPE);
	validate(output_pid=vfork(),OUTPUT_FORK);
	if(0==output_pid){
		close(pipefd[1]);
		dup2(pipefd[0],0);
		close(pipefd[0]);
		execlp("ffmpeg","-hide_banner","-ac","2","-ar","44100","-f","f32le","-i","-","-f","pulse","default",(char*)0);
	}

	char ss_buf[22]={'0',0,'0',0};
	while(1){

		if(ret_pid==output_pid){
			printf("WTF output died. RIP.\n");
			return(1);
		}

		else if(ret_pid==decode_pid){
			if(plc==++pos)
				goto quit;
			if(pos<0)
				pos=0;
			printf("\e[2K\r%s\n",&pl[pos][plen]);
			clock_gettime(CLOCK_MONOTONIC,&start);
			char* ss=ss_buf;
			if(state.off>0){
				start.tv_sec-=state.off;
				ss=&ss_buf[2];
				validate(snprintf(ss,20,"%d",state.off),STATE_READ_SPRINTF);
				state.off=0;
			}
			if(refresh_pid>0)
				kill(refresh_pid,SIGINT);
			validate(decode_pid=vfork(),DECODE_FORK);
			if(0==decode_pid){
				close(pipefd[0]);
				dup2(pipefd[1],1);
				close(pipefd[1]);
				execlp("ffmpeg","-hide_banner","-ss",ss,"-i",pl[pos],"-af","volume=0.75","-ac","2","-ar","44100","-f","f32le","-",(char*)0);
			}
		}

		else if(ret_pid==input_pid){
			switch(key=WEXITSTATUS(ret)){
			case 'Q':
				goto quit;
			case 'P':
				pos-=2; /* fallthrough */
			case 'N':
				kill(decode_pid,SIGINT);
			}
			validate(input_pid=fork(),INPUT_FORK);
			if(0==input_pid){
				read(0,&key,1);
				return((int)toupper(key));
			}
		}

		else if(ret_pid==refresh_pid){
			clock_gettime(CLOCK_MONOTONIC,&now);
			printf("\e[2K\r%d/%d %d:%02d > ",pos+1,plc,
				(now.tv_sec-start.tv_sec)/60,
				(now.tv_sec-start.tv_sec)%60
			);
			validate(refresh_pid=fork(),REFRESH_FORK);
			if(0==refresh_pid){
				usleep((start.tv_nsec+1E9-now.tv_nsec)/1E3);
				return(0);
			}
		}

		ret_pid= ret_pid<0 ? ret_pid+1 : wait(&ret);
	}

	quit:
	state.off=now.tv_sec-start.tv_sec;
	state.pos=pos;
	validate(fd=open(state_fn,O_WRONLY|O_CREAT|O_TRUNC,0775),STATE_WRITE_OPEN);
	validate(write(fd,&state,sizeof(statest)),STATE_WRITE);
	validate(close(fd),STATE_WRITE_CLOSE);

	if(refresh_pid>0) 
		kill(refresh_pid,SIGINT);
	if(decode_pid>0) 
		kill(decode_pid,SIGINT);
	if(output_pid>0) 
		kill(output_pid,SIGINT);
	if(input_pid>0) 
		kill(input_pid,SIGINT);
	printf("\e[2K\r    ]  " PKG " ✝ v" VERSION "  [    \n");
	return(0);
}
