#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "diagnostics/gate1a_identity/identity_copy_core.h"

typedef struct __attribute__((aligned(4))) HostCsc { uint8_t b[VBE_G1_CSC_SIZE]; } HostCsc;
typedef char host_size[(sizeof(HostCsc) == VBE_G1_CSC_SIZE) ? 1 : -1];
typedef char host_align[(__alignof__(HostCsc) >= 4) ? 1 : -1];

static int fail(const char *s){fprintf(stderr,"FAIL:%s\n",s);return 1;}
int main(void){
    HostCsc src,snap,copy;
    const void *bad=(const void *)(uintptr_t)1u;
    const void *p;
    unsigned i;
    VbeG1IdentityPrepareResult r;
    for(i=0;i<VBE_G1_CSC_SIZE;i++)src.b[i]=(uint8_t)(i*37u+11u);
    memset(&snap,0xA5,sizeof(snap));memset(&copy,0x5A,sizeof(copy));
    r=vbe_g1_prepare_identity(0,bad,&snap,&copy);
    if(r!=VBE_G1_PREP_INVALID_PLANE)return fail("invalid-result");
    for(i=0;i<VBE_G1_CSC_SIZE;i++)if(snap.b[i]!=0xA5||copy.b[i]!=0x5A)return fail("invalid-touched-storage");
    p=vbe_g1_forward_pointer(r,bad,&copy);if(p!=bad)return fail("invalid-passthrough");
    r=vbe_g1_prepare_identity(1,0,&snap,&copy);if(r!=VBE_G1_PREP_NULL)return fail("null-result");
    p=vbe_g1_forward_pointer(r,0,&copy);if(p!=0)return fail("null-passthrough");
    r=vbe_g1_prepare_identity(0,0,&snap,&copy);if(r!=VBE_G1_PREP_NULL)return fail("invalid-null-result");
    p=vbe_g1_forward_pointer(r,0,&copy);if(p!=0)return fail("invalid-null-passthrough");
    r=vbe_g1_prepare_identity(1,&src,&snap,&copy);if(r!=VBE_G1_PREP_SUBSTITUTE)return fail("identity-result");
    if(!vbe_g1_equal_3c(&src,&snap)||!vbe_g1_equal_3c(&src,&copy))return fail("identity-bytes");
    p=vbe_g1_forward_pointer(r,&src,&copy);if(p!=&copy)return fail("identity-substitution");
    copy.b[17]^=1u;
    r=VBE_G1_PREP_MISMATCH;p=vbe_g1_forward_pointer(r,&src,&copy);if(p!=&src)return fail("mismatch-not-fail-open");
    puts("GATE1A_IDENTITY_HOST=PASS");
    puts("INVALID_PLANE_NO_DEREFERENCE=PASS");
    puts("NULL_PASSTHROUGH=PASS");
    puts("MISMATCH_FAIL_OPEN=PASS");
    puts("ALL_0X3C_EXACT=PASS");
    puts("OWNED_COPY_ALIGNMENT=PASS");
    return 0;
}
