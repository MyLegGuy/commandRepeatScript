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

#define NULLDELIMFLAG "--nullDelim"
#define NEWLINEDELIMFLAG "--newlineDelim"
#define VERBOSETOGGLE "-v"
#define SUPERVERBOSETOGGLE "-V"
#define STARTDEFINEFLAG "-s"

#define PROP_SKIP_COMMAND_FORMAT_INDEX 1
#define PROP_CAN_FAIL 2
// #define			  4

volatile sig_atomic_t shouldHappyExit=0;
int lineDelim='\n';
char uselessInfo=0;
char reallyUselessInfo=0;

int curProperties=0;

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
	if (reallyUselessInfo){
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
int readNumberLine(FILE* fp, char** _tempLineBuff, size_t* _tempLineBuffSize){
	size_t _numRead = getdelim(_tempLineBuff,_tempLineBuffSize,lineDelim,fp);
	if (_numRead==-1){
		fputs("failed to read number line\n",stderr);
		exit(1);
	}
	// verify that all these characters are numbers
	int i;
	for (i=0;i<_numRead-1;++i){ // don't check the last character, that should be the deliminator character. if it's not the deliminator character, that means we're at EOF. this function is never used as the last line.
		if (!((*_tempLineBuff)[i]>='0' && (*_tempLineBuff)[i]<='9')){
			fprintf(stderr,"expected a number line, got %s\n",*_tempLineBuff);
			exit(1);			
		}
	}
	return atoi(*_tempLineBuff);
}
char runScript(FILE* fp, int _startIndex){
	char _ret=0;
	char* _tempLineBuff=NULL;
	size_t _tempLineBuffSize=0;
	// skip the first line by just reading and ignoring its contents
	if (getdelim(&_tempLineBuff,&_tempLineBuffSize,lineDelim,fp)==-1){
		fputs("error reading the ignored line!\n",stderr);
		exit(1);
	}
	// second line, script properties line
	curProperties = readNumberLine(fp,&_tempLineBuff,&_tempLineBuffSize);
	// third line, command format count
	int _totalCommandFormats = (curProperties & PROP_SKIP_COMMAND_FORMAT_INDEX) ? 1 : readNumberLine(fp,&_tempLineBuff,&_tempLineBuffSize);
	// Read command formats
	int* _commandSizes = malloc(sizeof(int)*_totalCommandFormats);
	char*** _commandLists = malloc(sizeof(char**)*_totalCommandFormats);
	int** _argMaps = malloc(sizeof(int*)*_totalCommandFormats);
	int* _argCounts = malloc(sizeof(int)*_totalCommandFormats);
	int _mostArgs=0; // most args out of any command format
	
	int c;
	for (c=0;c<_totalCommandFormats;++c){
		// Read initial command
		int _mainCommandSize = readNumberLine(fp,&_tempLineBuff,&_tempLineBuffSize);
		_commandSizes[c]=_mainCommandSize;
		char** _commandList = malloc(sizeof(char*)*(_mainCommandSize+1));
		_commandLists[c]=_commandList;
		int* _argMap = malloc(sizeof(int)*_mainCommandSize*2); // pattern with two byte info: <int destination index to put in _commandList> <int source index from _lastReadArgs>
		_argMaps[c]=_argMap;
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
		if (access(_commandList[0],F_OK|X_OK)!=0){
			fprintf(stderr,"%s permission denied\n",_commandList[0]);
			exit(1);
		}
		_argCounts[c]=_maxMapDigit+1;
		if (_argCounts[c]>_mostArgs){
			_mostArgs=_argCounts[c];
		}
	}

	// fast forward to start index of requested
	int _curCommandIndex=0;
	for (;_curCommandIndex<_startIndex;++_curCommandIndex){
		int _thisIndex = (curProperties & PROP_SKIP_COMMAND_FORMAT_INDEX) ? 0 : readNumberLine(fp,&_tempLineBuff,&_tempLineBuffSize);
		if (_thisIndex<0 || _thisIndex>=_totalCommandFormats){
			printf("out of range index\n");
			exit(1);
		}
		int i;
		for (i=0;i<_argCounts[_thisIndex];++i){
			while(1){
				int _gottenChar = fgetc(fp);
				if (_gottenChar==EOF){
					puts("read error when trying to find start index. is index too far?");
					exit(1);
				}else if (_gottenChar==lineDelim){
					break;
				}
			}
		}
	}
	// Read and do command sets
	char** _lastReadArgs = malloc(sizeof(char*)*_mostArgs);
	while(!feof(fp)){
		int _cIndex = (curProperties & PROP_SKIP_COMMAND_FORMAT_INDEX) ? 0 : readNumberLine(fp,&_tempLineBuff,&_tempLineBuffSize);
		// Read arguments into the _lastReadArgs array
		char _isScriptDone=0;
		int i;
		for (i=0;i<_argCounts[_cIndex];++i){
			size_t _readChars = getdelim(&_tempLineBuff,&_tempLineBuffSize,lineDelim,fp);
			if (_readChars==-1){
				if (feof(fp)){
					_isScriptDone=1;
					break;
				}else{
					fprintf(stderr,"error reading at set index %d\n",_curCommandIndex);
					exit(1);
				}
			}
			rmNewline(_tempLineBuff,_readChars);
			_lastReadArgs[i] = strdup(_tempLineBuff);
		}
		if (_isScriptDone){
			break;
		}
		// insert read special arguments into usual arguments array
		for (i=0;i<_argCounts[_cIndex];++i){
			_commandLists[_cIndex][_argMaps[_cIndex][i*2]]=_lastReadArgs[_argMaps[_cIndex][i*2+1]];
		}
		// execute
		if (runProgram(_commandLists[_cIndex],1)){
			if (!(curProperties & PROP_CAN_FAIL)){
				fprintf(stderr,"program failed at set index %d\n",_curCommandIndex);
				exit(1);
			}else{
				if (reallyUselessInfo){
					printf("Warning: failed.\n");
				}
			}
		}
		// free
		for (i=0;i<_argCounts[_cIndex];++i){
			free(_lastReadArgs[i]);
		}
		//
		#if NEWLINESEPARATE==1
		putchar('\n');
		#endif
		if (shouldHappyExit){
			if (uselessInfo){
				printf("happy exit at index %d\n",_curCommandIndex);
				_ret=1;
			}
			break;
		}
		++_curCommandIndex;
	}
	free(_lastReadArgs);
	if (uselessInfo){
		puts("happy single script end!");
	}
free:
	// free the command list
	for (c=0;c<_totalCommandFormats;++c){
		int i;
		// first set the already freed dynamic argument strings that are still inside _commandList to NULL
		for (i=0;i<_argCounts[c];++i){
			_commandLists[c][_argMaps[c][i*2]]=NULL;
		}
		for (i=0;i<_commandSizes[c];++i){
			free(_commandLists[c][i]);
		}
		free(_commandLists[c]);
	}
	free(_commandLists);
	for (c=0;c<_totalCommandFormats;++c){
		free(_argMaps[c]);
	}
	free(_argMaps);
	free(_commandSizes);
	free(_argCounts);	
	free(_tempLineBuff);
	return _ret;
}
int main(int argc, char** args){
	if (argc<2){
		fprintf(stderr,"Usage: %s [-V/-v] [-s <num>] ["NULLDELIMFLAG" / "NEWLINEDELIMFLAG"] <script 1> [script ...]\n",argc>=1 ? args[0] : "<program>");
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
	int _startIndex=0;
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
			reallyUselessInfo=0;
			++_nonScriptArgs;
			continue;
		}else if (strcmp(args[i],SUPERVERBOSETOGGLE)==0){
			reallyUselessInfo=!reallyUselessInfo;
			uselessInfo=reallyUselessInfo;
			++_nonScriptArgs;
			continue;
		}else if (strcmp(args[i],STARTDEFINEFLAG)==0){
			if (i!=argc-1){
				++i;
				_nonScriptArgs+=2;
				_startIndex=atoi(args[i]);
				continue;
			}else{
				puts(STARTDEFINEFLAG" needs an arg.");
				exit(1);
			}
		}
		if (uselessInfo){
			printf("Running script: %s (%d/%d)\n",args[i],i-_nonScriptArgs,argc-1-_nonScriptArgs);
		}
		FILE* fp = fopen(args[i],"rb");
		if (fp){
			if (runScript(fp,_startIndex)){
				i=argc;
			}
			_startIndex=0;
		}else{
			fprintf(stderr,"Could not open %s\n",args[i]);
			return 1;
		}
		fclose(fp);
	}
	return 0;
}

