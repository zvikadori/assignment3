#include "types.h"
#include "user.h"

void blabla(void){
	char *a = malloc(4096 * 2);
	int i=0;
	printf(0, "blablablablablablablablablablablablablablablablablablablablablablabla");
	for (i=0; i < 4096; i++)
		a[i] = 'a';
	for (; i< 2 * 4096; i++)
		a[i] = i;
		
	for (i =0 ; i < 2 * 4096; i++)
		printf(1, "%c", a[i]);
}

int main(void){
	char * z= malloc(4096*18);
	int i;
	void (*bla)(void) = blabla;
	printf(0, "hsdad\n\n\n");
	//for ( i=0; i < 4096 *15; i++)
		//z[i] = 'b';
		
	for ( i=0; i < 4096 *18; i++)
		printf(1, "%c", z[i]);
	fork();	
		
	bla();
	
	//
//	for(i=0; i< 4096 * 18; i++)
//		z[i] = 'b';
		//	for (i= 4096 * 10; i < 4096 * 25; i++)
	//	z[i] = 'b';
//	printf(1, "%s", z);
	exit();
	
	return 0;
}
