/* Schema integration tests for nr-rrc-15.6.0.asn1 — C API
 * Mirrors test_nr_rrc_15_6_0.cpp; covers valid round-trips,
 * constraint violations (encoder), and edge cases.
 * Types under test come from three generated modules:
 *   NR_RRC_Definitions, NR_UE_Variables, NR_InterNodeDefinitions
 */

#include "nr_rrc_15_6_0.h"
#include "runtime/c/asn1_bitwriter.h"
#include "runtime/c/asn1_bitreader.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── helpers ────────────────────────────────────────────────────────────────*/

static int g_passed = 0;
static int g_failed = 0;

static void report_pass(const char* name) {
    printf("[PASS] %s\n", name);
    ++g_passed;
}

static void report_fail(const char* name, const char* reason) {
    printf("[FAIL] %s : %s\n", name, reason);
    ++g_failed;
}

static void check_ok(const char* name, int rc, const char* err_buf) {
    if (rc == 0)
        report_pass(name);
    else
        report_fail(name, err_buf[0] ? err_buf : "unexpected failure (no message)");
}

static void check_fail(const char* name, int rc, const char* err_buf, const char* substr) {
    if (rc != 0) {
        if (strstr(err_buf, substr) != NULL) {
            report_pass(name);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "expected '%s' in error but got: %s", substr, err_buf);
            report_fail(name, msg);
        }
    } else {
        report_fail(name, "expected failure but encode/decode succeeded");
    }
}

/* Build a 36-bit CellIdentity from a 36-bit value (left-aligned in 5 bytes). */
static NR_RRC_Definitions_CellIdentity make_cell_identity(uint64_t val36) {
    NR_RRC_Definitions_CellIdentity ci;
    uint64_t shifted = val36 << 4;
    int i;
    ci.data = (uint8_t*)malloc(5);
    for (i = 4; i >= 0; --i)
        ci.data[4 - i] = (uint8_t)((shifted >> (i * 8)) & 0xFF);
    ci.bit_length = 36;
    return ci;
}

#define ERR_BUF_SIZE 256

/* ═══════════════════════════════════════════════════════════════════════════
 * 1.  NR_UE_Variables::VarShortMAC_Input
 *     Fields: sourcePhysCellId [0..1007], targetCellIdentity SIZE(36),
 *             source_c_RNTI [0..65535]
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_VarShortMAC_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarShortMAC_Input input;
    NR_UE_Variables_VarShortMAC_Input output;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    input.sourcePhysCellId   = 35;
    input.source_c_RNTI      = 17017;
    input.targetCellIdentity = make_cell_identity(0xABCDEF0ULL);

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarShortMAC_Input(&bw, &input, err, sizeof(err));
    if (rc != 0) { report_fail("VarShortMAC_Input: valid round-trip", err); goto cleanup1; }

    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_UE_Variables_decode_VarShortMAC_Input(&br, &output, err, sizeof(err));
    if (rc == 0) {
        assert(output.sourcePhysCellId == 35);
        assert(output.source_c_RNTI    == 17017);
        assert(output.targetCellIdentity.bit_length == 36);
        free(output.targetCellIdentity.data);
        report_pass("VarShortMAC_Input: valid round-trip");
    } else {
        report_fail("VarShortMAC_Input: valid round-trip", err);
    }
cleanup1:
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);
}

static void test_VarShortMAC_encode_cellIdentity_wrong_size(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarShortMAC_Input input;
    Asn1BitWriter bw;
    int rc;

    input.sourcePhysCellId            = 35;
    input.source_c_RNTI               = 100;
    input.targetCellIdentity.bit_length = 38;
    input.targetCellIdentity.data       = (uint8_t*)calloc(5, 1);

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarShortMAC_Input(&bw, &input, err, sizeof(err));
    check_fail("VarShortMAC_Input encode: CellIdentity bit_length=38 violates SIZE(36)",
               rc, err, "BIT STRING SIZE constraint violation");
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);
}

static void test_VarShortMAC_encode_physCellId_too_high(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarShortMAC_Input input;
    Asn1BitWriter bw;
    int rc;

    input.sourcePhysCellId   = 2000;
    input.source_c_RNTI      = 100;
    input.targetCellIdentity = make_cell_identity(1);

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarShortMAC_Input(&bw, &input, err, sizeof(err));
    check_fail("VarShortMAC_Input encode: sourcePhysCellId=2000 violates [0..1007]",
               rc, err, "INTEGER constraint violation");
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);
}

static void test_VarShortMAC_encode_physCellId_negative(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarShortMAC_Input input;
    Asn1BitWriter bw;
    int rc;

    input.sourcePhysCellId   = -1;
    input.source_c_RNTI      = 100;
    input.targetCellIdentity = make_cell_identity(1);

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarShortMAC_Input(&bw, &input, err, sizeof(err));
    check_fail("VarShortMAC_Input encode: sourcePhysCellId=-1 violates [0..1007]",
               rc, err, "INTEGER constraint violation");
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);
}

static void test_VarShortMAC_encode_rnti_too_high(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarShortMAC_Input input;
    Asn1BitWriter bw;
    int rc;

    input.sourcePhysCellId   = 100;
    input.source_c_RNTI      = 70000;
    input.targetCellIdentity = make_cell_identity(1);

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarShortMAC_Input(&bw, &input, err, sizeof(err));
    check_fail("VarShortMAC_Input encode: source_c_RNTI=70000 violates [0..65535]",
               rc, err, "INTEGER constraint violation");
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);
}

static void test_VarShortMAC_encode_boundary_values(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarShortMAC_Input input;
    Asn1BitWriter bw;
    int rc;

    /* min boundary */
    input.sourcePhysCellId   = 0;
    input.source_c_RNTI      = 0;
    input.targetCellIdentity = make_cell_identity(0);
    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarShortMAC_Input(&bw, &input, err, sizeof(err));
    check_ok("VarShortMAC_Input encode: min boundary values (0, 0, all-zero CellId)", rc, err);
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);

    /* max boundary */
    memset(err, 0, sizeof(err));
    input.sourcePhysCellId   = 1007;
    input.source_c_RNTI      = 65535;
    input.targetCellIdentity = make_cell_identity(0xFFFFFFFFFULL);
    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarShortMAC_Input(&bw, &input, err, sizeof(err));
    check_ok("VarShortMAC_Input encode: max boundary values (1007, 65535, all-one CellId)", rc, err);
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 2.  PhysCellId  (INTEGER 0..1007)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_PhysCellId_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_PhysCellId out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_PhysCellId(&bw, 500, err, sizeof(err));
    if (rc != 0) { report_fail("PhysCellId: valid round-trip (value=500)", err); goto done2a; }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_RRC_Definitions_decode_PhysCellId(&br, &out, err, sizeof(err));
    if (rc == 0) {
        assert(out == 500);
        report_pass("PhysCellId: valid round-trip (value=500)");
    } else {
        report_fail("PhysCellId: valid round-trip (value=500)", err);
    }
done2a:
    asn1_bw_free(&bw);
}

static void test_PhysCellId_roundtrip_boundaries(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_PhysCellId out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    /* min */
    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_PhysCellId(&bw, 0, err, sizeof(err));
    if (rc == 0) {
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_PhysCellId(&br, &out, err, sizeof(err));
        if (rc == 0) { assert(out == 0); report_pass("PhysCellId: round-trip at min (0)"); }
        else report_fail("PhysCellId: round-trip at min (0)", err);
    } else {
        report_fail("PhysCellId: round-trip at min (0)", err);
    }
    asn1_bw_free(&bw);

    /* max */
    memset(err, 0, sizeof(err));
    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_PhysCellId(&bw, 1007, err, sizeof(err));
    if (rc == 0) {
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_PhysCellId(&br, &out, err, sizeof(err));
        if (rc == 0) { assert(out == 1007); report_pass("PhysCellId: round-trip at max (1007)"); }
        else report_fail("PhysCellId: round-trip at max (1007)", err);
    } else {
        report_fail("PhysCellId: round-trip at max (1007)", err);
    }
    asn1_bw_free(&bw);
}

static void test_PhysCellId_encode_violation(void) {
    char err[ERR_BUF_SIZE] = {0};
    Asn1BitWriter bw;
    int rc;

    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_PhysCellId(&bw, 1008, err, sizeof(err));
    check_fail("PhysCellId encode: 1008 violates [0..1007]", rc, err, "INTEGER constraint violation");
    asn1_bw_free(&bw);

    memset(err, 0, sizeof(err));
    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_PhysCellId(&bw, -1, err, sizeof(err));
    check_fail("PhysCellId encode: -1 violates [0..1007]", rc, err, "INTEGER constraint violation");
    asn1_bw_free(&bw);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 3.  CellIdentity  (BIT STRING SIZE(36))
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_CellIdentity_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_CellIdentity ci_in, ci_out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    ci_in = make_cell_identity(0x123456789ULL & 0xFFFFFFFFFULL);
    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_CellIdentity(&bw, &ci_in, err, sizeof(err));
    if (rc != 0) { report_fail("CellIdentity: valid round-trip (36 bits)", err); goto done3a; }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_RRC_Definitions_decode_CellIdentity(&br, &ci_out, err, sizeof(err));
    if (rc == 0) {
        assert(ci_out.bit_length == 36);
        assert(memcmp(ci_out.data, ci_in.data, 5) == 0);
        free(ci_out.data);
        report_pass("CellIdentity: valid round-trip (36 bits)");
    } else {
        report_fail("CellIdentity: valid round-trip (36 bits)", err);
    }
done3a:
    free(ci_in.data);
    asn1_bw_free(&bw);
}

static void test_CellIdentity_encode_wrong_size(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_CellIdentity ci;
    Asn1BitWriter bw;
    int rc;

    ci.bit_length = 35;
    ci.data = (uint8_t*)calloc(5, 1);
    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_CellIdentity(&bw, &ci, err, sizeof(err));
    check_fail("CellIdentity encode: bit_length=35 violates SIZE(36)",
               rc, err, "BIT STRING SIZE constraint violation");
    asn1_bw_free(&bw);
    free(ci.data);

    memset(err, 0, sizeof(err));
    ci.bit_length = 40;
    ci.data = (uint8_t*)calloc(5, 1);
    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_CellIdentity(&bw, &ci, err, sizeof(err));
    check_fail("CellIdentity encode: bit_length=40 violates SIZE(36)",
               rc, err, "BIT STRING SIZE constraint violation");
    asn1_bw_free(&bw);
    free(ci.data);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 4.  VarPendingRNA_Update  (SEQUENCE, optional BOOLEAN)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_VarPendingRNA_roundtrip_absent(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarPendingRNA_Update val, out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    val.has_pendingRNA_Update = 0;
    val.pendingRNA_Update     = 0;

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarPendingRNA_Update(&bw, &val, err, sizeof(err));
    if (rc != 0) { report_fail("VarPendingRNA_Update: round-trip with pendingRNA_Update absent", err); goto done4a; }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_UE_Variables_decode_VarPendingRNA_Update(&br, &out, err, sizeof(err));
    if (rc == 0) {
        assert(out.has_pendingRNA_Update == 0);
        report_pass("VarPendingRNA_Update: round-trip with pendingRNA_Update absent");
    } else {
        report_fail("VarPendingRNA_Update: round-trip with pendingRNA_Update absent", err);
    }
done4a:
    asn1_bw_free(&bw);
}

static void test_VarPendingRNA_roundtrip_true(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarPendingRNA_Update val, out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    val.has_pendingRNA_Update = 1;
    val.pendingRNA_Update     = 1;

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarPendingRNA_Update(&bw, &val, err, sizeof(err));
    if (rc != 0) { report_fail("VarPendingRNA_Update: round-trip with pendingRNA_Update=true", err); goto done4b; }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_UE_Variables_decode_VarPendingRNA_Update(&br, &out, err, sizeof(err));
    if (rc == 0) {
        assert(out.has_pendingRNA_Update != 0);
        assert(out.pendingRNA_Update != 0);
        report_pass("VarPendingRNA_Update: round-trip with pendingRNA_Update=true");
    } else {
        report_fail("VarPendingRNA_Update: round-trip with pendingRNA_Update=true", err);
    }
done4b:
    asn1_bw_free(&bw);
}

static void test_VarPendingRNA_roundtrip_false(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarPendingRNA_Update val, out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    val.has_pendingRNA_Update = 1;
    val.pendingRNA_Update     = 0;

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarPendingRNA_Update(&bw, &val, err, sizeof(err));
    if (rc != 0) { report_fail("VarPendingRNA_Update: round-trip with pendingRNA_Update=false", err); goto done4c; }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_UE_Variables_decode_VarPendingRNA_Update(&br, &out, err, sizeof(err));
    if (rc == 0) {
        assert(out.has_pendingRNA_Update != 0);
        assert(out.pendingRNA_Update == 0);
        report_pass("VarPendingRNA_Update: round-trip with pendingRNA_Update=false");
    } else {
        report_fail("VarPendingRNA_Update: round-trip with pendingRNA_Update=false", err);
    }
done4c:
    asn1_bw_free(&bw);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 5.  RRCSetupRequest  (CHOICE inside SEQUENCE)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_RRCSetupRequest_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_RRCSetupRequest req;
    NR_RRC_Definitions_RRCSetupRequest out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    memset(&req, 0, sizeof(req));

    /* InitialUE-Identity CHOICE: ng-5G-S-TMSI-Part1 (BIT STRING SIZE(39)) */
    req.rrcSetupRequest.ue_Identity.tag = NR_RRC_Definitions_InitialUE_Identity_TAG_ng_5G_S_TMSI_Part1;
    req.rrcSetupRequest.ue_Identity.u.ng_5G_S_TMSI_Part1.bit_length = 39;
    req.rrcSetupRequest.ue_Identity.u.ng_5G_S_TMSI_Part1.data = (uint8_t*)malloc(5);
    memset(req.rrcSetupRequest.ue_Identity.u.ng_5G_S_TMSI_Part1.data, 0xAA, 5);

    req.rrcSetupRequest.establishmentCause = NR_RRC_Definitions_EstablishmentCause_mo_Signalling;

    req.rrcSetupRequest.spare.bit_length = 1;
    req.rrcSetupRequest.spare.data = (uint8_t*)calloc(1, 1);

    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_RRCSetupRequest(&bw, &req, err, sizeof(err));
    if (rc != 0) {
        report_fail("RRCSetupRequest: valid round-trip (ng-5G-S-TMSI-Part1 choice)", err);
        goto done5;
    }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_RRC_Definitions_decode_RRCSetupRequest(&br, &out, err, sizeof(err));
    if (rc == 0) {
        report_pass("RRCSetupRequest: valid round-trip (ng-5G-S-TMSI-Part1 choice)");
        free(out.rrcSetupRequest.ue_Identity.u.ng_5G_S_TMSI_Part1.data);
        free(out.rrcSetupRequest.spare.data);
    } else {
        report_fail("RRCSetupRequest: valid round-trip (ng-5G-S-TMSI-Part1 choice)", err);
    }
done5:
    free(req.rrcSetupRequest.ue_Identity.u.ng_5G_S_TMSI_Part1.data);
    free(req.rrcSetupRequest.spare.data);
    asn1_bw_free(&bw);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 6.  VarResumeMAC_Input  (same fields as VarShortMAC_Input)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_VarResumeMAC_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarResumeMAC_Input input, out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    input.sourcePhysCellId   = 512;
    input.source_c_RNTI      = 32768;
    input.targetCellIdentity = make_cell_identity(0x1FFFFFFFFULL & 0xFFFFFFFFFULL);

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarResumeMAC_Input(&bw, &input, err, sizeof(err));
    if (rc != 0) { report_fail("VarResumeMAC_Input: valid round-trip", err); goto done6a; }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_UE_Variables_decode_VarResumeMAC_Input(&br, &out, err, sizeof(err));
    if (rc == 0) {
        assert(out.sourcePhysCellId == 512);
        assert(out.source_c_RNTI    == 32768);
        assert(out.targetCellIdentity.bit_length == 36);
        free(out.targetCellIdentity.data);
        report_pass("VarResumeMAC_Input: valid round-trip");
    } else {
        report_fail("VarResumeMAC_Input: valid round-trip", err);
    }
done6a:
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);
}

static void test_VarResumeMAC_encode_cellIdentity_wrong_size(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarResumeMAC_Input input;
    Asn1BitWriter bw;
    int rc;

    input.sourcePhysCellId              = 10;
    input.source_c_RNTI                 = 10;
    input.targetCellIdentity.bit_length = 32;
    input.targetCellIdentity.data       = (uint8_t*)calloc(4, 1);

    asn1_bw_init(&bw);
    rc = NR_UE_Variables_encode_VarResumeMAC_Input(&bw, &input, err, sizeof(err));
    check_fail("VarResumeMAC_Input encode: CellIdentity bit_length=32 violates SIZE(36)",
               rc, err, "BIT STRING SIZE constraint violation");
    free(input.targetCellIdentity.data);
    asn1_bw_free(&bw);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 7.  Bitstream integrity: same input → same bytes
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_deterministic_encoding(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_UE_Variables_VarShortMAC_Input inp1, inp2;
    Asn1BitWriter bw1, bw2;
    size_t sz;
    int ok = 1;

    inp1.sourcePhysCellId   = 100;
    inp1.source_c_RNTI      = 1234;
    inp1.targetCellIdentity = make_cell_identity(0xDEADBEEFULL & 0xFFFFFFFFFULL);

    inp2.sourcePhysCellId   = 100;
    inp2.source_c_RNTI      = 1234;
    inp2.targetCellIdentity = make_cell_identity(0xDEADBEEFULL & 0xFFFFFFFFFULL);

    asn1_bw_init(&bw1);
    asn1_bw_init(&bw2);
    NR_UE_Variables_encode_VarShortMAC_Input(&bw1, &inp1, err, sizeof(err));
    NR_UE_Variables_encode_VarShortMAC_Input(&bw2, &inp2, err, sizeof(err));

    sz = asn1_bw_get_buffer_size(&bw1);
    if (sz != asn1_bw_get_buffer_size(&bw2)) {
        ok = 0;
    } else {
        size_t i;
        const uint8_t* b1 = asn1_bw_get_buffer(&bw1);
        const uint8_t* b2 = asn1_bw_get_buffer(&bw2);
        for (i = 0; i < sz; ++i) {
            if (b1[i] != b2[i]) { ok = 0; break; }
        }
    }
    if (ok) report_pass("Deterministic encoding: same input -> same bytes");
    else    report_fail("Deterministic encoding: same input -> same bytes", "byte mismatch");

    free(inp1.targetCellIdentity.data);
    free(inp2.targetCellIdentity.data);
    asn1_bw_free(&bw1);
    asn1_bw_free(&bw2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 8.  ENUMERATED round-trips  (ReestablishmentCause)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_ReestablishmentCause_all_values(void) {
    static const NR_RRC_Definitions_ReestablishmentCause values[] = {
        NR_RRC_Definitions_ReestablishmentCause_reconfigurationFailure,
        NR_RRC_Definitions_ReestablishmentCause_handoverFailure,
        NR_RRC_Definitions_ReestablishmentCause_otherFailure,
        NR_RRC_Definitions_ReestablishmentCause_spare1,
    };
    static const char* names[] = {
        "reconfigurationFailure", "handoverFailure",
        "otherFailure", "spare1",
    };
    int i;
    for (i = 0; i < 4; ++i) {
        char err[ERR_BUF_SIZE] = {0};
        char label[128];
        NR_RRC_Definitions_ReestablishmentCause out;
        Asn1BitWriter bw;
        Asn1BitReader br;
        int rc;

        snprintf(label, sizeof(label), "ReestablishmentCause round-trip: %s", names[i]);

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_ReestablishmentCause(&bw, values[i], err, sizeof(err));
        if (rc != 0) { report_fail(label, err); asn1_bw_free(&bw); continue; }
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_ReestablishmentCause(&br, &out, err, sizeof(err));
        if (rc == 0) {
            assert(out == values[i]);
            report_pass(label);
        } else {
            report_fail(label, err);
        }
        asn1_bw_free(&bw);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 9.  Nested SEQUENCE  (RRCReestablishmentRequest)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_RRCReestablishmentRequest_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_RRCReestablishmentRequest req, out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    memset(&req, 0, sizeof(req));
    req.rrcReestablishmentRequest.ue_Identity.c_RNTI     = 1000;
    req.rrcReestablishmentRequest.ue_Identity.physCellId = 500;
    req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.bit_length = 16;
    req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data = (uint8_t*)malloc(2);
    req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data[0] = 0xAB;
    req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data[1] = 0xCD;
    req.rrcReestablishmentRequest.reestablishmentCause =
        NR_RRC_Definitions_ReestablishmentCause_handoverFailure;
    req.rrcReestablishmentRequest.spare.bit_length = 1;
    req.rrcReestablishmentRequest.spare.data = (uint8_t*)calloc(1, 1);

    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_RRCReestablishmentRequest(&bw, &req, err, sizeof(err));
    if (rc != 0) {
        report_fail("RRCReestablishmentRequest: valid round-trip", err);
        goto done9a;
    }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_RRC_Definitions_decode_RRCReestablishmentRequest(&br, &out, err, sizeof(err));
    if (rc == 0) {
        assert(out.rrcReestablishmentRequest.ue_Identity.c_RNTI     == 1000);
        assert(out.rrcReestablishmentRequest.ue_Identity.physCellId == 500);
        assert(out.rrcReestablishmentRequest.reestablishmentCause ==
               NR_RRC_Definitions_ReestablishmentCause_handoverFailure);
        free(out.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data);
        free(out.rrcReestablishmentRequest.spare.data);
        report_pass("RRCReestablishmentRequest: valid round-trip (nested SEQUENCE + ENUMERATED)");
    } else {
        report_fail("RRCReestablishmentRequest: valid round-trip", err);
    }
done9a:
    free(req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data);
    free(req.rrcReestablishmentRequest.spare.data);
    asn1_bw_free(&bw);
}

static void test_RRCReestablishmentRequest_constraint_violations(void) {
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_RRCReestablishmentRequest req;
        Asn1BitWriter bw;
        int rc;

        memset(&req, 0, sizeof(req));
        req.rrcReestablishmentRequest.ue_Identity.c_RNTI     = 0;
        req.rrcReestablishmentRequest.ue_Identity.physCellId = 1008; /* out of [0..1007] */
        req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.bit_length = 16;
        req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data = (uint8_t*)calloc(2, 1);
        req.rrcReestablishmentRequest.reestablishmentCause =
            NR_RRC_Definitions_ReestablishmentCause_spare1;
        req.rrcReestablishmentRequest.spare.bit_length = 1;
        req.rrcReestablishmentRequest.spare.data = (uint8_t*)calloc(1, 1);

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_RRCReestablishmentRequest(&bw, &req, err, sizeof(err));
        check_fail("RRCReestablishmentRequest: physCellId=1008 in nested SEQUENCE",
                   rc, err, "INTEGER constraint violation");
        free(req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data);
        free(req.rrcReestablishmentRequest.spare.data);
        asn1_bw_free(&bw);
    }
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_RRCReestablishmentRequest req;
        Asn1BitWriter bw;
        int rc;

        memset(&req, 0, sizeof(req));
        req.rrcReestablishmentRequest.ue_Identity.c_RNTI     = 0;
        req.rrcReestablishmentRequest.ue_Identity.physCellId = 0;
        req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.bit_length = 8; /* wrong, must be 16 */
        req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data = (uint8_t*)calloc(1, 1);
        req.rrcReestablishmentRequest.reestablishmentCause =
            NR_RRC_Definitions_ReestablishmentCause_spare1;
        req.rrcReestablishmentRequest.spare.bit_length = 1;
        req.rrcReestablishmentRequest.spare.data = (uint8_t*)calloc(1, 1);

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_RRCReestablishmentRequest(&bw, &req, err, sizeof(err));
        check_fail("RRCReestablishmentRequest: shortMAC-I bit_length=8 violates SIZE(16)",
                   rc, err, "BIT STRING SIZE constraint violation");
        free(req.rrcReestablishmentRequest.ue_Identity.shortMAC_I.data);
        free(req.rrcReestablishmentRequest.spare.data);
        asn1_bw_free(&bw);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 10. BOOLEAN field  (MasterKeyUpdate)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_MasterKeyUpdate_roundtrip(void) {
    /* BOOLEAN true, optional octet string present */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_MasterKeyUpdate ku, out;
        Asn1BitWriter bw;
        Asn1BitReader br;
        int rc;

        memset(&ku, 0, sizeof(ku));
        ku.keySetChangeIndicator = 1;
        ku.nextHopChainingCount  = 3;
        ku.has_nas_Container     = 1;
        ku.nas_Container.length  = 4;
        ku.nas_Container.data    = (uint8_t*)malloc(4);
        ku.nas_Container.data[0] = 0xDE;
        ku.nas_Container.data[1] = 0xAD;
        ku.nas_Container.data[2] = 0xBE;
        ku.nas_Container.data[3] = 0xEF;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_MasterKeyUpdate(&bw, &ku, err, sizeof(err));
        if (rc != 0) {
            report_fail("MasterKeyUpdate: round-trip (BOOLEAN true, optional octet string present)", err);
            goto done10a;
        }
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_MasterKeyUpdate(&br, &out, err, sizeof(err));
        if (rc == 0) {
            assert(out.keySetChangeIndicator != 0);
            assert(out.nextHopChainingCount  == 3);
            assert(out.has_nas_Container != 0);
            free(out.nas_Container.data);
            report_pass("MasterKeyUpdate: round-trip (BOOLEAN true, optional octet string present)");
        } else {
            report_fail("MasterKeyUpdate: round-trip (BOOLEAN true, optional octet string present)", err);
        }
done10a:
        free(ku.nas_Container.data);
        asn1_bw_free(&bw);
    }
    /* BOOLEAN false, optional absent */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_MasterKeyUpdate ku, out;
        Asn1BitWriter bw;
        Asn1BitReader br;
        int rc;

        memset(&ku, 0, sizeof(ku));
        ku.keySetChangeIndicator = 0;
        ku.nextHopChainingCount  = 0;
        ku.has_nas_Container     = 0;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_MasterKeyUpdate(&bw, &ku, err, sizeof(err));
        if (rc != 0) {
            report_fail("MasterKeyUpdate: round-trip (BOOLEAN false, optional absent)", err);
            asn1_bw_free(&bw);
            return;
        }
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_MasterKeyUpdate(&br, &out, err, sizeof(err));
        if (rc == 0) {
            assert(out.keySetChangeIndicator == 0);
            assert(out.nextHopChainingCount  == 0);
            assert(out.has_nas_Container == 0);
            report_pass("MasterKeyUpdate: round-trip (BOOLEAN false, optional absent)");
        } else {
            report_fail("MasterKeyUpdate: round-trip (BOOLEAN false, optional absent)", err);
        }
        asn1_bw_free(&bw);
    }
}

static void test_MasterKeyUpdate_integer_constraint(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_MasterKeyUpdate ku;
    Asn1BitWriter bw;
    int rc;

    memset(&ku, 0, sizeof(ku));
    ku.keySetChangeIndicator = 0;
    ku.nextHopChainingCount  = 8; /* out of [0..7] */

    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_MasterKeyUpdate(&bw, &ku, err, sizeof(err));
    check_fail("MasterKeyUpdate: nextHopChainingCount=8 violates [0..7]",
               rc, err, "INTEGER constraint violation");
    asn1_bw_free(&bw);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 11. SEQUENCE OF with SIZE constraints (MCC, MNC, DRB-CountMSB-InfoList)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_MCC_MNC_roundtrip(void) {
    /* MCC present, MNC 2-digit */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_PLMN_Identity plmn, out;
        NR_RRC_Definitions_MCC_MNC_Digit mcc_items[3] = {2, 0, 4};
        NR_RRC_Definitions_MCC_MNC_Digit mnc_items[2] = {1, 5};
        Asn1BitWriter bw;
        Asn1BitReader br;
        int rc;

        plmn.has_mcc     = 1;
        plmn.mcc.items   = mcc_items;
        plmn.mcc.count   = 3;
        plmn.mnc.items   = mnc_items;
        plmn.mnc.count   = 2;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_PLMN_Identity(&bw, &plmn, err, sizeof(err));
        if (rc != 0) {
            report_fail("PLMN-Identity round-trip (MCC present, MNC 2 digits)", err);
            asn1_bw_free(&bw);
            goto plmn2;
        }
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_PLMN_Identity(&br, &out, err, sizeof(err));
        if (rc == 0) {
            assert(out.has_mcc != 0);
            assert(out.mcc.count == 3);
            assert(out.mcc.items[0] == 2);
            assert(out.mcc.items[1] == 0);
            assert(out.mcc.items[2] == 4);
            assert(out.mnc.count == 2);
            assert(out.mnc.items[0] == 1);
            assert(out.mnc.items[1] == 5);
            free(out.mcc.items);
            free(out.mnc.items);
            report_pass("PLMN-Identity round-trip (MCC present, MNC 2 digits)");
        } else {
            report_fail("PLMN-Identity round-trip (MCC present, MNC 2 digits)", err);
        }
        asn1_bw_free(&bw);
    }
plmn2:
    /* MCC absent, MNC 3-digit */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_PLMN_Identity plmn, out;
        NR_RRC_Definitions_MCC_MNC_Digit mnc_items[3] = {3, 1, 0};
        Asn1BitWriter bw;
        Asn1BitReader br;
        int rc;

        plmn.has_mcc   = 0;
        plmn.mnc.items = mnc_items;
        plmn.mnc.count = 3;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_PLMN_Identity(&bw, &plmn, err, sizeof(err));
        if (rc != 0) {
            report_fail("PLMN-Identity round-trip (MCC absent, MNC 3 digits)", err);
            asn1_bw_free(&bw);
            return;
        }
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_PLMN_Identity(&br, &out, err, sizeof(err));
        if (rc == 0) {
            assert(out.has_mcc == 0);
            assert(out.mnc.count == 3);
            assert(out.mnc.items[0] == 3);
            assert(out.mnc.items[1] == 1);
            assert(out.mnc.items[2] == 0);
            free(out.mnc.items);
            report_pass("PLMN-Identity round-trip (MCC absent, MNC 3 digits)");
        } else {
            report_fail("PLMN-Identity round-trip (MCC absent, MNC 3 digits)", err);
        }
        asn1_bw_free(&bw);
    }
}

static void test_MCC_fixed_size_violation(void) {
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_MCC mcc;
        NR_RRC_Definitions_MCC_MNC_Digit items[2] = {2, 0};
        Asn1BitWriter bw;
        int rc;

        mcc.items = items;
        mcc.count = 2;
        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_MCC(&bw, &mcc, err, sizeof(err));
        check_fail("MCC encode: 2 digits violates SIZE(3)",
                   rc, err, "SEQUENCE OF SIZE constraint violation");
        asn1_bw_free(&bw);
    }
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_MCC mcc;
        NR_RRC_Definitions_MCC_MNC_Digit items[4] = {2, 0, 4, 0};
        Asn1BitWriter bw;
        int rc;

        mcc.items = items;
        mcc.count = 4;
        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_MCC(&bw, &mcc, err, sizeof(err));
        check_fail("MCC encode: 4 digits violates SIZE(3)",
                   rc, err, "SEQUENCE OF SIZE constraint violation");
        asn1_bw_free(&bw);
    }
}

static void test_MNC_range_size_violation(void) {
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_MNC mnc;
        NR_RRC_Definitions_MCC_MNC_Digit items[1] = {5};
        Asn1BitWriter bw;
        int rc;

        mnc.items = items;
        mnc.count = 1;
        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_MNC(&bw, &mnc, err, sizeof(err));
        check_fail("MNC encode: 1 digit violates SIZE(2..3)",
                   rc, err, "SEQUENCE OF SIZE constraint violation");
        asn1_bw_free(&bw);
    }
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_MNC mnc;
        NR_RRC_Definitions_MCC_MNC_Digit items[4] = {1, 2, 3, 4};
        Asn1BitWriter bw;
        int rc;

        mnc.items = items;
        mnc.count = 4;
        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_MNC(&bw, &mnc, err, sizeof(err));
        check_fail("MNC encode: 4 digits violates SIZE(2..3)",
                   rc, err, "SEQUENCE OF SIZE constraint violation");
        asn1_bw_free(&bw);
    }
}

static void test_DRB_CountMSB_InfoList_constraint(void) {
    /* empty list violates SIZE(1..29) */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_DRB_CountMSB_InfoList lst;
        Asn1BitWriter bw;
        int rc;

        lst.items = NULL;
        lst.count = 0;
        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_DRB_CountMSB_InfoList(&bw, &lst, err, sizeof(err));
        check_fail("DRB-CountMSB-InfoList encode: empty list violates SIZE(1..29)",
                   rc, err, "SEQUENCE OF SIZE constraint violation");
        asn1_bw_free(&bw);
    }
    /* 30 elements violates SIZE(1..29) */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_DRB_CountMSB_InfoList lst;
        NR_RRC_Definitions_DRB_CountMSB_Info items[30];
        Asn1BitWriter bw;
        int rc, i;

        for (i = 0; i < 30; ++i) {
            items[i].drb_Identity      = 1;
            items[i].countMSB_Uplink   = 0;
            items[i].countMSB_Downlink = 0;
        }
        lst.items = items;
        lst.count = 30;
        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_DRB_CountMSB_InfoList(&bw, &lst, err, sizeof(err));
        check_fail("DRB-CountMSB-InfoList encode: 30 elements violates SIZE(1..29)",
                   rc, err, "SEQUENCE OF SIZE constraint violation");
        asn1_bw_free(&bw);
    }
    /* Valid: 1 element */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_DRB_CountMSB_InfoList lst, out;
        NR_RRC_Definitions_DRB_CountMSB_Info item;
        Asn1BitWriter bw;
        Asn1BitReader br;
        int rc;

        item.drb_Identity      = 1;
        item.countMSB_Uplink   = 33554431; /* 2^25 - 1 */
        item.countMSB_Downlink = 0;
        lst.items = &item;
        lst.count = 1;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_DRB_CountMSB_InfoList(&bw, &lst, err, sizeof(err));
        if (rc != 0) {
            report_fail("DRB-CountMSB-InfoList: round-trip with 1 element (min SIZE)", err);
            asn1_bw_free(&bw);
            return;
        }
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_DRB_CountMSB_InfoList(&br, &out, err, sizeof(err));
        if (rc == 0) {
            assert(out.count == 1);
            assert(out.items[0].drb_Identity      == 1);
            assert(out.items[0].countMSB_Uplink   == 33554431);
            assert(out.items[0].countMSB_Downlink == 0);
            free(out.items);
            report_pass("DRB-CountMSB-InfoList: round-trip with 1 element (min SIZE)");
        } else {
            report_fail("DRB-CountMSB-InfoList: round-trip with 1 element (min SIZE)", err);
        }
        asn1_bw_free(&bw);
    }
}

static void test_DRB_Identity_constraint(void) {
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_DRB_CountMSB_InfoList lst;
        NR_RRC_Definitions_DRB_CountMSB_Info item;
        Asn1BitWriter bw;
        int rc;

        item.drb_Identity      = 0; /* below min of 1 */
        item.countMSB_Uplink   = 0;
        item.countMSB_Downlink = 0;
        lst.items = &item;
        lst.count = 1;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_DRB_CountMSB_InfoList(&bw, &lst, err, sizeof(err));
        check_fail("DRB-Identity encode: 0 violates [1..32]", rc, err, "INTEGER constraint violation");
        asn1_bw_free(&bw);
    }
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_DRB_CountMSB_InfoList lst;
        NR_RRC_Definitions_DRB_CountMSB_Info item;
        Asn1BitWriter bw;
        int rc;

        item.drb_Identity      = 33; /* above max of 32 */
        item.countMSB_Uplink   = 0;
        item.countMSB_Downlink = 0;
        lst.items = &item;
        lst.count = 1;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_DRB_CountMSB_InfoList(&bw, &lst, err, sizeof(err));
        check_fail("DRB-Identity encode: 33 violates [1..32]", rc, err, "INTEGER constraint violation");
        asn1_bw_free(&bw);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 12. CHOICE with multiple alternatives  (PagingUE-Identity)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_PagingUE_Identity_ng5g_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_PagingUE_Identity pui, out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    pui.tag = NR_RRC_Definitions_PagingUE_Identity_TAG_ng_5G_S_TMSI;
    pui.u.ng_5G_S_TMSI.bit_length = 48;
    pui.u.ng_5G_S_TMSI.data = (uint8_t*)malloc(6);
    pui.u.ng_5G_S_TMSI.data[0] = 0x12;
    pui.u.ng_5G_S_TMSI.data[1] = 0x34;
    pui.u.ng_5G_S_TMSI.data[2] = 0x56;
    pui.u.ng_5G_S_TMSI.data[3] = 0x78;
    pui.u.ng_5G_S_TMSI.data[4] = 0x9A;
    pui.u.ng_5G_S_TMSI.data[5] = 0xBC;

    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_PagingUE_Identity(&bw, &pui, err, sizeof(err));
    if (rc != 0) { report_fail("PagingUE-Identity: round-trip (ng-5G-S-TMSI, 48 bits)", err); goto done12a; }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_RRC_Definitions_decode_PagingUE_Identity(&br, &out, err, sizeof(err));
    if (rc == 0) {
        assert(out.tag == NR_RRC_Definitions_PagingUE_Identity_TAG_ng_5G_S_TMSI);
        assert(out.u.ng_5G_S_TMSI.bit_length == 48);
        assert(memcmp(out.u.ng_5G_S_TMSI.data, pui.u.ng_5G_S_TMSI.data, 6) == 0);
        free(out.u.ng_5G_S_TMSI.data);
        report_pass("PagingUE-Identity: round-trip (ng-5G-S-TMSI, 48 bits)");
    } else {
        report_fail("PagingUE-Identity: round-trip (ng-5G-S-TMSI, 48 bits)", err);
    }
done12a:
    free(pui.u.ng_5G_S_TMSI.data);
    asn1_bw_free(&bw);
}

static void test_PagingUE_Identity_irnti_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_PagingUE_Identity pui, out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    pui.tag = NR_RRC_Definitions_PagingUE_Identity_TAG_fullI_RNTI;
    pui.u.fullI_RNTI.bit_length = 40;
    pui.u.fullI_RNTI.data = (uint8_t*)malloc(5);
    pui.u.fullI_RNTI.data[0] = 0xAA;
    pui.u.fullI_RNTI.data[1] = 0xBB;
    pui.u.fullI_RNTI.data[2] = 0xCC;
    pui.u.fullI_RNTI.data[3] = 0xDD;
    pui.u.fullI_RNTI.data[4] = 0xEE;

    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_PagingUE_Identity(&bw, &pui, err, sizeof(err));
    if (rc != 0) { report_fail("PagingUE-Identity: round-trip (fullI-RNTI, 40 bits)", err); goto done12b; }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_RRC_Definitions_decode_PagingUE_Identity(&br, &out, err, sizeof(err));
    if (rc == 0) {
        assert(out.tag == NR_RRC_Definitions_PagingUE_Identity_TAG_fullI_RNTI);
        assert(out.u.fullI_RNTI.bit_length == 40);
        assert(memcmp(out.u.fullI_RNTI.data, pui.u.fullI_RNTI.data, 5) == 0);
        free(out.u.fullI_RNTI.data);
        report_pass("PagingUE-Identity: round-trip (fullI-RNTI, 40 bits)");
    } else {
        report_fail("PagingUE-Identity: round-trip (fullI-RNTI, 40 bits)", err);
    }
done12b:
    free(pui.u.fullI_RNTI.data);
    asn1_bw_free(&bw);
}

static void test_PagingUE_Identity_bitstring_size_violation(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_RRC_Definitions_PagingUE_Identity pui;
    Asn1BitWriter bw;
    int rc;

    pui.tag = NR_RRC_Definitions_PagingUE_Identity_TAG_ng_5G_S_TMSI;
    pui.u.ng_5G_S_TMSI.bit_length = 32; /* wrong, must be 48 */
    pui.u.ng_5G_S_TMSI.data = (uint8_t*)calloc(4, 1);

    asn1_bw_init(&bw);
    rc = NR_RRC_Definitions_encode_PagingUE_Identity(&bw, &pui, err, sizeof(err));
    check_fail("PagingUE-Identity ng-5G-S-TMSI: bit_length=32 violates SIZE(48)",
               rc, err, "BIT STRING SIZE constraint violation");
    free(pui.u.ng_5G_S_TMSI.data);
    asn1_bw_free(&bw);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 13. PagingRecord  (SEQUENCE with optional ENUMERATED + extension marker)
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_PagingRecord_roundtrip(void) {
    /* ng-5G-S-TMSI identity, accessType present */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_PagingRecord rec, out;
        Asn1BitWriter bw;
        Asn1BitReader br;
        int rc;

        memset(&rec, 0, sizeof(rec));
        rec.ue_Identity.tag = NR_RRC_Definitions_PagingUE_Identity_TAG_ng_5G_S_TMSI;
        rec.ue_Identity.u.ng_5G_S_TMSI.bit_length = 48;
        rec.ue_Identity.u.ng_5G_S_TMSI.data = (uint8_t*)malloc(6);
        rec.ue_Identity.u.ng_5G_S_TMSI.data[0] = 0x01;
        rec.ue_Identity.u.ng_5G_S_TMSI.data[1] = 0x02;
        rec.ue_Identity.u.ng_5G_S_TMSI.data[2] = 0x03;
        rec.ue_Identity.u.ng_5G_S_TMSI.data[3] = 0x04;
        rec.ue_Identity.u.ng_5G_S_TMSI.data[4] = 0x05;
        rec.ue_Identity.u.ng_5G_S_TMSI.data[5] = 0x06;
        rec.has_accessType = 1;
        rec.accessType = NR_RRC_Definitions_PagingRecord_accessType_type_non3GPP;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_PagingRecord(&bw, &rec, err, sizeof(err));
        if (rc != 0) {
            report_fail("PagingRecord: round-trip (ng-5G-S-TMSI identity, accessType present)", err);
            goto done13a;
        }
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_PagingRecord(&br, &out, err, sizeof(err));
        if (rc == 0) {
            assert(out.ue_Identity.tag == NR_RRC_Definitions_PagingUE_Identity_TAG_ng_5G_S_TMSI);
            assert(out.has_accessType != 0);
            assert(out.accessType == NR_RRC_Definitions_PagingRecord_accessType_type_non3GPP);
            free(out.ue_Identity.u.ng_5G_S_TMSI.data);
            report_pass("PagingRecord: round-trip (ng-5G-S-TMSI identity, accessType present)");
        } else {
            report_fail("PagingRecord: round-trip (ng-5G-S-TMSI identity, accessType present)", err);
        }
done13a:
        free(rec.ue_Identity.u.ng_5G_S_TMSI.data);
        asn1_bw_free(&bw);
    }
    /* fullI-RNTI identity, accessType absent */
    {
        char err[ERR_BUF_SIZE] = {0};
        NR_RRC_Definitions_PagingRecord rec, out;
        Asn1BitWriter bw;
        Asn1BitReader br;
        int rc;

        memset(&rec, 0, sizeof(rec));
        rec.ue_Identity.tag = NR_RRC_Definitions_PagingUE_Identity_TAG_fullI_RNTI;
        rec.ue_Identity.u.fullI_RNTI.bit_length = 40;
        rec.ue_Identity.u.fullI_RNTI.data = (uint8_t*)malloc(5);
        rec.ue_Identity.u.fullI_RNTI.data[0] = 0x11;
        rec.ue_Identity.u.fullI_RNTI.data[1] = 0x22;
        rec.ue_Identity.u.fullI_RNTI.data[2] = 0x33;
        rec.ue_Identity.u.fullI_RNTI.data[3] = 0x44;
        rec.ue_Identity.u.fullI_RNTI.data[4] = 0x55;
        rec.has_accessType = 0;

        asn1_bw_init(&bw);
        rc = NR_RRC_Definitions_encode_PagingRecord(&bw, &rec, err, sizeof(err));
        if (rc != 0) {
            report_fail("PagingRecord: round-trip (accessType absent)", err);
            goto done13b;
        }
        asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
        rc = NR_RRC_Definitions_decode_PagingRecord(&br, &out, err, sizeof(err));
        if (rc == 0) {
            assert(out.has_accessType == 0);
            free(out.ue_Identity.u.fullI_RNTI.data);
            report_pass("PagingRecord: round-trip (accessType absent)");
        } else {
            report_fail("PagingRecord: round-trip (accessType absent)", err);
        }
done13b:
        free(rec.ue_Identity.u.fullI_RNTI.data);
        asn1_bw_free(&bw);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * 14. Cross-module references  (NR_InterNodeDefinitions)
 *     MeasurementTimingConfiguration → c1/measTimingConf path
 * ═══════════════════════════════════════════════════════════════════════════*/

static void test_MeasurementTimingConfiguration_roundtrip(void) {
    char err[ERR_BUF_SIZE] = {0};
    NR_InterNodeDefinitions_MeasurementTimingConfiguration msg;
    NR_InterNodeDefinitions_MeasurementTimingConfiguration out;
    Asn1BitWriter bw;
    Asn1BitReader br;
    int rc;

    memset(&msg, 0, sizeof(msg));
    msg.criticalExtensions.tag =
        NR_InterNodeDefinitions_MeasurementTimingConfiguration_criticalExtensions_type_TAG_c1;
    msg.criticalExtensions.u.c1.tag =
        NR_InterNodeDefinitions_MeasurementTimingConfiguration_criticalExtensions_type_c1_type_TAG_measTimingConf;
    msg.criticalExtensions.u.c1.u.measTimingConf.has_measTiming = 0;
    msg.criticalExtensions.u.c1.u.measTimingConf.has_nonCriticalExtension = 0;

    asn1_bw_init(&bw);
    rc = NR_InterNodeDefinitions_encode_MeasurementTimingConfiguration(&bw, &msg, err, sizeof(err));
    if (rc != 0) {
        report_fail("MeasurementTimingConfiguration: round-trip (empty measTiming list)", err);
        asn1_bw_free(&bw);
        return;
    }
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    rc = NR_InterNodeDefinitions_decode_MeasurementTimingConfiguration(&br, &out, err, sizeof(err));
    if (rc == 0) {
        report_pass("MeasurementTimingConfiguration: round-trip (empty measTiming list)");
    } else {
        report_fail("MeasurementTimingConfiguration: round-trip (empty measTiming list)", err);
    }
    asn1_bw_free(&bw);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * entry point
 * ═══════════════════════════════════════════════════════════════════════════*/

int main(void) {
    printf("=== nr-rrc-15.6.0 C schema integration tests ===\n\n");

    test_VarShortMAC_roundtrip();
    test_VarShortMAC_encode_cellIdentity_wrong_size();
    test_VarShortMAC_encode_physCellId_too_high();
    test_VarShortMAC_encode_physCellId_negative();
    test_VarShortMAC_encode_rnti_too_high();
    test_VarShortMAC_encode_boundary_values();

    test_PhysCellId_roundtrip();
    test_PhysCellId_roundtrip_boundaries();
    test_PhysCellId_encode_violation();

    test_CellIdentity_roundtrip();
    test_CellIdentity_encode_wrong_size();

    test_VarPendingRNA_roundtrip_absent();
    test_VarPendingRNA_roundtrip_true();
    test_VarPendingRNA_roundtrip_false();

    test_RRCSetupRequest_roundtrip();

    test_VarResumeMAC_roundtrip();
    test_VarResumeMAC_encode_cellIdentity_wrong_size();

    test_deterministic_encoding();

    test_ReestablishmentCause_all_values();

    test_RRCReestablishmentRequest_roundtrip();
    test_RRCReestablishmentRequest_constraint_violations();

    test_MasterKeyUpdate_roundtrip();
    test_MasterKeyUpdate_integer_constraint();

    test_MCC_MNC_roundtrip();
    test_MCC_fixed_size_violation();
    test_MNC_range_size_violation();
    test_DRB_CountMSB_InfoList_constraint();
    test_DRB_Identity_constraint();

    test_PagingUE_Identity_ng5g_roundtrip();
    test_PagingUE_Identity_irnti_roundtrip();
    test_PagingUE_Identity_bitstring_size_violation();

    test_PagingRecord_roundtrip();

    test_MeasurementTimingConfiguration_roundtrip();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
