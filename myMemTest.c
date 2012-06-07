#include "types.h"
#include "user.h"
int main(void){
	char * z= malloc(4096*18);
	int i;
	for ( i= 0; i < 4096 *15; i++)
		z[i] = 'a';
		
	printf(0, "\n\n\n");
	for(i=0; i< 4096 * 18; i++)
		z[i] = 'b';
		//	for (i= 4096 * 10; i < 4096 * 25; i++)
	//	z[i] = 'b';
//	printf(1, "%s", z);
	exit();
	
	return 0;
}
