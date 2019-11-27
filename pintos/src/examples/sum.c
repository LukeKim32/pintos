/* sum.c

   첫번째 파라미터 => 번째의 피보나치 값 (fibonacci())
   첫번째 ~ 네번째 파라미터 => 총 합 (sum_of_four_int()) */

#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include <stdlib.h>

#define NOT_A_NUMBER -2147483647
#define ERROR -1
#define POSITIVE 0
#define NEGATIVE 1

int
main (int argc, char **argv) 
{
  bool success = EXIT_SUCCESS;
   
  if(argc!=5){
      return EXIT_FAILURE;
  }

  int a = atoi(argv[1]);
  int b = atoi(argv[2]);
  int c = atoi(argv[3]);
  int d = atoi(argv[4]);
  
  if(a<0){
    printf("fibonacci target index must be positive\n");
    return EXIT_FAILURE;
  }

  int fibonacciResult = fibonacci(a);
  int sum = sum_of_four_int(a,b,c,d);

  if(fibonacciResult <0){
     printf("피보나치 결과값이 int type의 범위를 벗어납니다.  %d\n",fibonacciResult);
  }else{

    printf("%d %d\n",fibonacciResult,sum);
  }


  return success;

}


