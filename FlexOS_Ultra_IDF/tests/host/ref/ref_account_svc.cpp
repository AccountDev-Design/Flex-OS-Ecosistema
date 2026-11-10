// Referencia del SERVICIO: FlexOS_Account.cpp (Arduino) entero, con la API publica exportada con nombres planos (sin tipos de Arduino) para que la prueba "lockstep" la
// maneje con la misma interfaz que el puerto en C. El nucleo (ref_acct_*) sale de ref_account_main.cpp.
#include "ref_account_main.cpp"

extern "C" {
typedef struct {
    int state, progress, linked;
    char stage[72], error[144], code[9], activation_url[192], flex_address[48], display_name[64];
    int link;
    char link_detail[96];
    unsigned verified_age_s;
} ref_snap_t;

void ref_svc_begin() { flexAccountBegin(); }
int ref_svc_request_code(const char *label) { return flexAccountRequestCode(label) ? 1 : 0; }
void ref_svc_cancel() { flexAccountCancel(); }
int ref_svc_linked() { return flexAccountLinked() ? 1 : 0; }
int ref_svc_usable() { return flexAccountUsable() ? 1 : 0; }
int ref_svc_state() { return (int)flexAccountState(); }
int ref_svc_link_state() { return (int)flexAccountLinkState(); }
void ref_svc_snapshot(ref_snap_t *o)
{
    FlexAccountSnapshot s;
    flexAccountSnapshot(&s);
    o->state = (int)s.state;
    o->progress = s.progress;
    o->linked = s.linked ? 1 : 0;
    memcpy(o->stage, s.stage, sizeof(o->stage));
    memcpy(o->error, s.error, sizeof(o->error));
    memcpy(o->code, s.code, sizeof(o->code));
    memcpy(o->activation_url, s.activationUrl, sizeof(o->activation_url));
    memcpy(o->flex_address, s.flexAddress, sizeof(o->flex_address));
    memcpy(o->display_name, s.displayName, sizeof(o->display_name));
    o->link = (int)s.link;
    memcpy(o->link_detail, s.linkDetail, sizeof(o->link_detail));
    o->verified_age_s = s.verifiedAgeS;
}
void ref_svc_request_validation() { flexAccountRequestValidation(); }
void ref_svc_report_rejected() { flexAccountReportRejected(); }
int ref_svc_copy_bearer(char *out, unsigned long cap) { return flexAccountCopyBearer(out, cap) ? 1 : 0; }
void ref_svc_forget_local() { flexAccountForgetLocal(); }
void ref_svc_step() { flexAccountTestStep(); }
void ref_svc_power_cycle() { flexAccountTestPowerCycle(); }
}
