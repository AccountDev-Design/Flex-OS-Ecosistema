// La foto de Flex Account con tipos planos: lo que cruza entre las referencias de Arduino (C++) y las pruebas en C.
#pragma once
typedef struct {
    int state, progress, linked;
    char stage[72], error[144], code[9], activation_url[192], flex_address[48], display_name[64];
    int link;
    char link_detail[96];
    unsigned verified_age_s;
} ref_snap_t;
