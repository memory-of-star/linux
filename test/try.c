#include <stdio.h>

int main(){
    // __uint64_t a = 3*((__uint64_t)(0x4000))*((__uint64_t)(0x10000));
    __uint64_t a = 3*0x40000000;
    printf("%lld\n", a);
    return 0;
}