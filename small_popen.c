/*
 * $Log: small_popen.c,v $
 * Revision 1.1  1995/12/14 15:23:30  tjd
 * Initial revision
 *
 */

#include <stdio.h>
#include <unistd.h>

int small_popen_r(char *cmd[])
{
	int fds[2];

	if(pipe(fds)==-1)
	{
		perror("pipe");
		return -1;
	}

	switch(fork())
	{
		case -1:
			perror("fork (small_popen)");
			return -1;

		case 0:
			close(fds[0]);
			if(dup2(fds[1],1)==-1)
			{
				perror("dup2");
				return -1;
			}

			execvp(cmd[0],cmd);
			perror("exec (small_popen)");
			return -1;

		default:
			close(fds[1]);
			return fds[0];
	}

	return -1;	/* NOTREACHED */
}
