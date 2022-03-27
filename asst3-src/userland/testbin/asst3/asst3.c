/* super simple test program */

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "unistd.h"

/*
static const char *string1 = "ASST3 Hello World!!";

int func(int,int);

int func(int a, int b){
	return 5*a + b*b*2;
}*/

void f(int*);

void f(int*p){
	p[2] = 6;
}

#if 0
int main()
{
	//printf("hi from user\n");
	int pid = fork();
	
	//printf("J: %d %s\n", pid, s);
	if (pid !=0) {
		int retcode, flags=0;
		waitpid(pid, &retcode, flags);
	}
	else{
		char* s = malloc(50 * sizeof(char));
		char** args = malloc(50 * sizeof(char*));
		args[0] = malloc(50 * sizeof(char));
		args[1] = malloc(50 * sizeof(char));
		args[2] = malloc(50 * sizeof(char));
		args[3] = NULL;
		strcpy(s, "./bin/cp");
		strcpy(args[0], "./bin/cp");
		strcpy(args[1], "./catfile");
		strcpy(args[2], "./newcatfile");
		
		//args[0] = malloc(50 * sizeof(char));
		execv(s, args);
	}
	//printf("   X\n");
	/*
	int a[] = {1,2,3};
	printf("num: %d\n", a[2]);
	f(a);
	printf("num: %d\n", a[2]);*/
	//exit(0);
}

#else
int main()
{

	int fd = open("test1.txt",O_RDWR|O_CREAT);
	char a[20]="ghpafdhg\n4h9qg37g\nht";
	lseek(fd,0,SEEK_SET);
	close(fd);

	fd = open("test1.txt",O_RDWR|O_CREAT);
	char c[10];
	char *b;
	write(fd,a,20);
	read(fd,c,10);
	printf("%s\n",a);
	printf("%s\n",c);

	b = mmap(10,0,fd,0);
	printf("%d\n",*b);

	printf("%s\n",b);
	close(fd);
	return 0;
}


#endif