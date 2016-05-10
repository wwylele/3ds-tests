#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#define EXP_SIZE 11ULL
#define EXP_SHIFT 52ULL
#define EXP_MASK (((1ULL << EXP_SIZE) - 1ULL) << EXP_SHIFT)

int main() {

  // Initialize the seed and open a log file
  srand(555);
  FILE* f = fopen("out.txt", "w");

  // 100k rounds should be enough
  unsigned int rounds = 100000;

  for(unsigned int i = 0; i < rounds; i++) {

    // We'll do 5 different cases for simplicity

    // 0: vdm_exponent < 1022 (neither)
    // 1: vdm_exponent = 1022 (vdm_exponent >= 1023 - 1)
    // 2: vdm_exponent = 1023 (vdm_exponent >= 1023 but also >= 1023 - 1)
    // 3: vdm_exponent = rand()
    // 4: vdm_exponent = rand() in [-5;+34]

    unsigned int type = i % 5;
    int exp = 0;
    switch(type) {
      case 0:
        exp = rand() % 1022;
        break;
      case 1:
        exp = 1022;
        break;
      case 2:
        exp = 1023;
        break;
      case 3:
        exp = rand();
        break;
      case 4:
        exp = (rand() % 40) - 5;
        break;
      default:
        fprintf(f, "Unsupported mode %d!\n", type);
        break;
    }

    // Generate random mantissa + sign and replace the exp by what we chose
    uint64_t in = (uint64_t)rand() << 32 | (uint64_t)rand();
    in &= ~EXP_MASK;
    in |= ((uint64_t)(exp + 1023) << EXP_SHIFT) & EXP_MASK;

    uint32_t out_u;
    uint32_t out_s;

    asm volatile("vldr d0, %2\n"
                 "ftouid s0, d0\n"
                 "ftosid s1, d0\n"
                 "vstr s0, %0\n"
                 "vstr s1, %1\n"
                 : "=m"(out_u), "=m"(out_s)
                 : "m"(in));

    fprintf(f, "in: 0x%016" PRIX64 "\ttype: %d\tfptoui: 0x%08" PRIX32 "\tfptosi: 0x%08" PRIX32 "\n", in, type, out_u, out_s);

  }

  // Close the log file
  fclose(f);

  return 0;

}