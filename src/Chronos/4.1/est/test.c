#include <stdio.h>

typedef unsigned long long ticks;

static __inline__ ticks getticks(void)
{
     unsigned a, d;
     asm volatile("rdtsc" : "=a" (a), "=d" (d));
     return ((ticks)a) | (((ticks)d) << 32);
}

int main()
{
		  ticks a,b;
		  
		  a = getticks();
		  sleep(10);
		  b = getticks();

		  printf("Time = %lf\n", (b - a)/((1.0)*2533821000));
}
