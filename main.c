#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

// Set these to 0 to disable useless output to stdout
#define NEWLINESEPARATE 0
#define PRINTCOMMANDBEFORE 1
#define HAPPY 1
#define PRINTSCRIPTNAME 1

#define NULLDELIMFLAG "--nullDelim"
#define NEWLINEDELIMFLAG "--newlineDelim"
#define VERBOSETOGGLE "-v"

volatile sig_atomic_t shouldHappyExit=0;
int lineDelim='\n';
char uselessInfo=0;

// Catch SIGUSR1
static void happyExitCatch(const int signo){
	(void)signo;	
	shouldHappyExit=1;
}
char rmNewline(char* _target, size_t _len){ // _len includes the newline character
	--_len;
	if (_target[_len]=='\n'){
		_target[_len]='\0';
		return 1;
	}
	return 0;
}
// returns 1 if irregular exit
char getExitStatus(int _exitInfo){
	if (WIFEXITED(_exitInfo)){
		if (WEXITSTATUS(_exitInfo)!=0){
			return 1;
		}else{
			return 0;
		}
	}else{
		return 1;
	}
}
char runProgram(char* const _args[], char _showOutput){
	if (uselessInfo){
		char** _tempArgs=(char**)_args;
		while(_tempArgs[0]){
			printf("%s ",_tempArgs[0]);
			_tempArgs++;
		}
		putchar('\n');
	}
	pid_t _newProcess;
	if ((_newProcess=fork())==0) {
		if (!_showOutput){
			int _nullFile = open("/dev/null", O_WRONLY);
			dup2(_nullFile,STDOUT_FILENO);
			dup2(_nullFile,STDERR_FILENO);
			close(_nullFile);
		}
		execv(_args[0],_args);
		exit(1);
	}
	char _happySeen=0;
	while(1){
		int _exitInfo;
		if (waitpid(_newProcess,&_exitInfo,0)==-1){
			if (_happySeen){ // only allow shouldHappyExit to repeat waitpid once
				return 1;
			}
			if (errno==EINTR && shouldHappyExit){ // if we caught SIGUSR1, don't interrupt. repeat.
				_happySeen=1; 
				continue;
			}
		}
		return getExitStatus(_exitInfo);
	}
}
void runScript(FILE* fp){
	char* _tempLineBuff=NULL;
	size_t _tempLineBuffSize=0;
	// skip the first line by just reading and ignoring its contents
	if (getdelim(&_tempLineBuff,&_tempLineBuffSize,lineDelim,fp)==-1){
		fputs("error reading the ignored line!\n",stderr);
		exit(1);
	}
	// Read initial command
	if (getdelim(&_tempLineBuff,&_tempLineBuffSize,lineDelim,fp)==-1){
		fputs("failed to read first command line, count\n",stderr);
		exit(1);
	}
	int _mainCommandSize = atoi(_tempLineBuff);
	char** _commandList = malloc(sizeof(char*)*(_mainCommandSize+1));
	int* _argMap = malloc(sizeof(int)*_mainCommandSize*2); // pattern with two byte info: <int destination index to put in _commandList> <int source index from _lastReadArgs>
	int _numInsertions=0; // use this to find how much of _argMap to read
	int _maxMapDigit=-1;
	int i;
	for (i=0;i<_mainCommandSize;++i){
		size_t _readChars = getdelim(&_tempLineBuff,&_tempLineBuffSize,lineDelim,fp);
		if (_readChars==-1){
			fputs("Failed to read line when parsing command\n",stderr);
			exit(1);
		}
		if (_tempLineBuff[0]=='$'){
			int _insertSource = atoi(&_tempLineBuff[1]);
			if (_insertSource>_maxMapDigit){
				_maxMapDigit=_insertSource;
				_argMap[_numInsertions*2]=i;
				_argMap[_numInsertions*2+1]=_insertSource;
				_numInsertions++;
			}
		}else{
			rmNewline(_tempLineBuff,_readChars);
			_commandList[i]=strdup(_tempLineBuff);
		}
	}
	_commandList[_mainCommandSize]=NULL; // null terminated array
	if (_numInsertions==0 || _mainCommandSize==0){
		fputs("??? - no insertions makes this program useless.\n",stderr);
		goto free;
	}
	if (access(_commandList[0],F_OK|X_OK)!=0){
		printf("%s permission denied\n",_commandList[0]);
		exit(1);
	}

	// Read and do commands
	int _numDifferentArgs=_maxMapDigit+1;
	char** _lastReadArgs = malloc(sizeof(char*)*_numDifferentArgs);
	while(!feof(fp)){
		for (i=0;i<_numDifferentArgs;++i){
			size_t _readChars = getdelim(&_tempLineBuff,&_tempLineBuffSize,lineDelim,fp);
			if (_readChars==-1){
				fputs("error reading!\n",stderr);
				exit(1);
			}
			rmNewline(_tempLineBuff,_readChars);
			_lastReadArgs[i] = strdup(_tempLineBuff);
		}
		// insert read special arguments into usual arguments array
		for (i=0;i<_numInsertions;++i){
			_commandList[_argMap[i*2]]=_lastReadArgs[_argMap[i*2+1]];
		}
		// execute
		if (runProgram(_commandList,1)){
			fputs("program failed!\n",stderr);
			exit(1);
		}
		// free
		for (i=0;i<_numDifferentArgs;++i){
			free(_lastReadArgs[i]);
		}
		//
		#if NEWLINESEPARATE==1
		putchar('\n');
		#endif
		if (shouldHappyExit){
			if (uselessInfo){
				puts("happy exit");
			}
			break;
		}
	}
	free(_lastReadArgs);
	if (uselessInfo){
		puts("happy single script end!");
	}
free:
	// free the command list
	// first set the already freed dynamic argument strings that are still inside _commandList to NULL
	for (i=0;i<_numInsertions;++i){
		_commandList[_argMap[i*2]]=NULL;
	}
	for (i=0;i<_mainCommandSize;++i){
		free(_commandList[i]);
	}
	free(_argMap);
	free(_commandList);
	free(_tempLineBuff);
}
int main(int argc, char** args){
	if (argc<2){
		fprintf(stderr,"Usage: %s [-v] ["NULLDELIMFLAG" / "NEWLINEDELIMFLAG"] <script 1> [script ...]\n",argc>=1 ? args[0] : "<program>");
		return 1;
	}
	// Catch SIGUSR1. Use it as a signal to finish up before exiting
	struct sigaction exitCatchSig;
	memset(&exitCatchSig, 0, sizeof(struct sigaction));
	exitCatchSig.sa_handler = happyExitCatch;
	sigaction(SIGUSR1, &exitCatchSig, NULL);
	// Run passed scripts
	int _nonScriptArgs=0;
	int i;
	for (i=1;i<argc;++i){
		if (strcmp(args[i],NULLDELIMFLAG)==0){
			lineDelim='\0';
			++_nonScriptArgs;
			continue;
		}else if (strcmp(args[i],NEWLINEDELIMFLAG)==0){
			lineDelim='\n';
			++_nonScriptArgs;
			continue;
		}else if (strcmp(args[i],VERBOSETOGGLE)==0){
			uselessInfo=!uselessInfo;
			++_nonScriptArgs;
			continue;
		}
		if (uselessInfo){
			printf("Running script: %s (%d/%d)\n",args[i],i-_nonScriptArgs,argc-1-_nonScriptArgs);
		}
		FILE* fp = fopen(args[i],"rb");
		if (fp){
			runScript(fp);
		}else{
			fprintf(stderr,"Could not open %s\n",args[i]);
			return 1;
		}
		fclose(fp);
	}
	return 0;
}

