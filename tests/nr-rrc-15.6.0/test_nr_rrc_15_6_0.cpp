// Schema integration tests for nr-rrc-15.6.0.asn1
// Covers: valid round-trips, constraint violations (encoder + decoder), edge cases.
// Types under test come from three generated namespaces:
//   asn1::generated::NR_RRC_Definitions
//   asn1::generated::NR_UE_Variables
//   asn1::generated::NR_InterNodeDefinitions

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nr_rrc_15_6_0.h"
#include "runtime/core/BitReader.h"
#include "runtime/core/BitWriter.h"

using namespace asn1::generated;
using namespace asn1::runtime;

// ─── helpers ────────────────────────────────────────────────────────────────

static int passed = 0;
static int failed = 0;

static void ok(const std::string& name) {
    std::cout << "[PASS] " << name << "\n";
    ++passed;
}

static void fail(const std::string& name, const std::string& reason) {
    std::cout << "[FAIL] " << name << " : " << reason << "\n";
    ++failed;
}

// Run f(); expect no exception → PASS.
template <typename F>
static void expect_ok(const std::string& name, F&& f) {
    try {
        f();
        ok(name);
    } catch (const std::exception& e) {
        fail(name, std::string("unexpected exception: ") + e.what());
    }
}

// Run f(); expect std::runtime_error whose message contains `substr` → PASS.
template <typename F>
static void expect_throw(const std::string& name, const std::string& substr, F&& f) {
    try {
        f();
        fail(name, "expected exception was not thrown");
    } catch (const std::runtime_error& e) {
        const std::string msg = e.what();
        if (msg.find(substr) != std::string::npos) {
            ok(name);
        } else {
            fail(name, "exception thrown but message mismatch: " + msg);
        }
    } catch (const std::exception& e) {
        fail(name, std::string("wrong exception type: ") + e.what());
    }
}

// Build a 36-bit CellIdentity BitString from a 36-bit value.
static NR_RRC_Definitions::CellIdentity makeCellIdentity(uint64_t val36) {
    NR_RRC_Definitions::CellIdentity ci;
    ci.bit_length = 36;
    uint64_t shifted = val36 << 4; // left-align into 5 bytes
    for (int i = 4; i >= 0; --i)
        ci.data.push_back(static_cast<uint8_t>((shifted >> (i * 8)) & 0xFF));
    return ci;
}

// ═══════════════════════════════════════════════════════════════════════════
// 1.  NR_UE_Variables::VarShortMAC_Input
//     Fields: sourcePhysCellId [0..1007], targetCellIdentity SIZE(36),
//             source_c_RNTI [0..65535]
// ═══════════════════════════════════════════════════════════════════════════

static void test_VarShortMAC_roundtrip() {
    expect_ok("VarShortMAC_Input: valid round-trip", []() {
        NR_UE_Variables::VarShortMAC_Input input;
        input.sourcePhysCellId = 35;
        input.source_c_RNTI    = 17017;
        input.targetCellIdentity = makeCellIdentity(0xABCDEF0ULL);

        BitWriter bw;
        NR_UE_Variables::encode_VarShortMAC_Input(bw, input);

        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto output = NR_UE_Variables::decode_VarShortMAC_Input(br);

        assert(output.sourcePhysCellId     == input.sourcePhysCellId);
        assert(output.source_c_RNTI        == input.source_c_RNTI);
        assert(output.targetCellIdentity.bit_length == 36);
    });
}

static void test_VarShortMAC_encode_cellIdentity_wrong_size() {
    expect_throw(
        "VarShortMAC_Input encode: CellIdentity bit_length=38 violates SIZE(36)",
        "BIT STRING SIZE constraint violation",
        []() {
            NR_UE_Variables::VarShortMAC_Input input;
            input.sourcePhysCellId   = 35;
            input.source_c_RNTI      = 100;
            input.targetCellIdentity.bit_length = 38; // wrong — must be 36
            input.targetCellIdentity.data = {0x00, 0x00, 0x00, 0x00, 0x00};

            BitWriter bw;
            NR_UE_Variables::encode_VarShortMAC_Input(bw, input);
        });
}

static void test_VarShortMAC_encode_physCellId_too_high() {
    expect_throw(
        "VarShortMAC_Input encode: sourcePhysCellId=2000 violates [0..1007]",
        "INTEGER constraint violation",
        []() {
            NR_UE_Variables::VarShortMAC_Input input;
            input.sourcePhysCellId   = 2000; // out of [0..1007]
            input.source_c_RNTI      = 100;
            input.targetCellIdentity = makeCellIdentity(1);

            BitWriter bw;
            NR_UE_Variables::encode_VarShortMAC_Input(bw, input);
        });
}

static void test_VarShortMAC_encode_physCellId_negative() {
    expect_throw(
        "VarShortMAC_Input encode: sourcePhysCellId=-1 violates [0..1007]",
        "INTEGER constraint violation",
        []() {
            NR_UE_Variables::VarShortMAC_Input input;
            input.sourcePhysCellId   = -1;
            input.source_c_RNTI      = 100;
            input.targetCellIdentity = makeCellIdentity(1);

            BitWriter bw;
            NR_UE_Variables::encode_VarShortMAC_Input(bw, input);
        });
}

static void test_VarShortMAC_encode_rnti_too_high() {
    expect_throw(
        "VarShortMAC_Input encode: source_c_RNTI=70000 violates [0..65535]",
        "INTEGER constraint violation",
        []() {
            NR_UE_Variables::VarShortMAC_Input input;
            input.sourcePhysCellId   = 100;
            input.source_c_RNTI      = 70000; // out of [0..65535]
            input.targetCellIdentity = makeCellIdentity(1);

            BitWriter bw;
            NR_UE_Variables::encode_VarShortMAC_Input(bw, input);
        });
}

static void test_VarShortMAC_encode_boundary_values() {
    expect_ok("VarShortMAC_Input encode: min boundary values (0, 0, all-zero CellId)", []() {
        NR_UE_Variables::VarShortMAC_Input input;
        input.sourcePhysCellId   = 0;
        input.source_c_RNTI      = 0;
        input.targetCellIdentity = makeCellIdentity(0);

        BitWriter bw;
        NR_UE_Variables::encode_VarShortMAC_Input(bw, input);
    });

    expect_ok("VarShortMAC_Input encode: max boundary values (1007, 65535, all-one CellId)", []() {
        NR_UE_Variables::VarShortMAC_Input input;
        input.sourcePhysCellId   = 1007;
        input.source_c_RNTI      = 65535;
        input.targetCellIdentity = makeCellIdentity(0xFFFFFFFFFULL); // 36 bits all-1

        BitWriter bw;
        NR_UE_Variables::encode_VarShortMAC_Input(bw, input);
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// 2.  NR_RRC_Definitions::PhysCellId  (INTEGER 0..1007)
// ═══════════════════════════════════════════════════════════════════════════

static void test_PhysCellId_roundtrip() {
    expect_ok("PhysCellId: valid round-trip (value=500)", []() {
        BitWriter bw;
        NR_RRC_Definitions::encode_PhysCellId(bw, 500);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        [[maybe_unused]] auto out = NR_RRC_Definitions::decode_PhysCellId(br);
        assert(out == 500);
    });
}

static void test_PhysCellId_roundtrip_boundaries() {
    expect_ok("PhysCellId: round-trip at min (0)", []() {
        BitWriter bw;
        NR_RRC_Definitions::encode_PhysCellId(bw, 0);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        [[maybe_unused]] auto out = NR_RRC_Definitions::decode_PhysCellId(br);
        assert(out == 0);
    });

    expect_ok("PhysCellId: round-trip at max (1007)", []() {
        BitWriter bw;
        NR_RRC_Definitions::encode_PhysCellId(bw, 1007);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        [[maybe_unused]] auto out = NR_RRC_Definitions::decode_PhysCellId(br);
        assert(out == 1007);
    });
}

static void test_PhysCellId_encode_violation() {
    expect_throw("PhysCellId encode: 1008 violates [0..1007]", "INTEGER constraint violation", []() {
        BitWriter bw;
        NR_RRC_Definitions::encode_PhysCellId(bw, 1008);
    });

    expect_throw("PhysCellId encode: -1 violates [0..1007]", "INTEGER constraint violation", []() {
        BitWriter bw;
        NR_RRC_Definitions::encode_PhysCellId(bw, -1);
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// 3.  NR_RRC_Definitions::CellIdentity  (BIT STRING SIZE(36))
// ═══════════════════════════════════════════════════════════════════════════

static void test_CellIdentity_roundtrip() {
    expect_ok("CellIdentity: valid round-trip (36 bits)", []() {
        auto ci_in = makeCellIdentity(0x123456789ULL & 0xFFFFFFFFFULL);

        BitWriter bw;
        NR_RRC_Definitions::encode_CellIdentity(bw, ci_in);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto ci_out = NR_RRC_Definitions::decode_CellIdentity(br);

        assert(ci_out.bit_length == 36);
        assert(ci_out.data == ci_in.data);
    });
}

static void test_CellIdentity_encode_wrong_size() {
    expect_throw("CellIdentity encode: bit_length=35 violates SIZE(36)", "BIT STRING SIZE constraint violation", []() {
        NR_RRC_Definitions::CellIdentity ci;
        ci.bit_length = 35;
        ci.data = {0x00, 0x00, 0x00, 0x00, 0x00};
        BitWriter bw;
        NR_RRC_Definitions::encode_CellIdentity(bw, ci);
    });

    expect_throw("CellIdentity encode: bit_length=40 violates SIZE(36)", "BIT STRING SIZE constraint violation", []() {
        NR_RRC_Definitions::CellIdentity ci;
        ci.bit_length = 40;
        ci.data = {0x00, 0x00, 0x00, 0x00, 0x00};
        BitWriter bw;
        NR_RRC_Definitions::encode_CellIdentity(bw, ci);
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// 4.  NR_UE_Variables::VarPendingRNA_Update  (SEQUENCE, optional BOOLEAN)
// ═══════════════════════════════════════════════════════════════════════════

static void test_VarPendingRNA_roundtrip_absent() {
    expect_ok("VarPendingRNA_Update: round-trip with pendingRNA_Update absent", []() {
        NR_UE_Variables::VarPendingRNA_Update val;

        BitWriter bw;
        NR_UE_Variables::encode_VarPendingRNA_Update(bw, val);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        [[maybe_unused]] NR_UE_Variables::VarPendingRNA_Update out =
            NR_UE_Variables::decode_VarPendingRNA_Update(br);

        assert(!out.pendingRNA_Update.has_value());
    });
}

static void test_VarPendingRNA_roundtrip_true() {
    expect_ok("VarPendingRNA_Update: round-trip with pendingRNA_Update=true", []() {
        NR_UE_Variables::VarPendingRNA_Update val;
        val.pendingRNA_Update = true;

        BitWriter bw;
        NR_UE_Variables::encode_VarPendingRNA_Update(bw, val);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        [[maybe_unused]] NR_UE_Variables::VarPendingRNA_Update out =
            NR_UE_Variables::decode_VarPendingRNA_Update(br);

        assert(out.pendingRNA_Update.has_value());
        assert(out.pendingRNA_Update.value() == true);
    });
}

static void test_VarPendingRNA_roundtrip_false() {
    expect_ok("VarPendingRNA_Update: round-trip with pendingRNA_Update=false", []() {
        NR_UE_Variables::VarPendingRNA_Update val;
        val.pendingRNA_Update = false;

        BitWriter bw;
        NR_UE_Variables::encode_VarPendingRNA_Update(bw, val);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        [[maybe_unused]] NR_UE_Variables::VarPendingRNA_Update out =
            NR_UE_Variables::decode_VarPendingRNA_Update(br);

        assert(out.pendingRNA_Update.has_value());
        assert(out.pendingRNA_Update.value() == false);
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// 5.  NR_RRC_Definitions::RRCSetupRequest  (CHOICE inside SEQUENCE)
// ═══════════════════════════════════════════════════════════════════════════

static void test_RRCSetupRequest_roundtrip() {
    expect_ok("RRCSetupRequest: valid round-trip (ng-5G-S-TMSI-Part1 choice)", []() {
        NR_RRC_Definitions::RRCSetupRequest req;

        // InitialUE-Identity CHOICE: ng-5G-S-TMSI-Part1 (BIT STRING SIZE(39))
        NR_RRC_Definitions::InitialUE_Identity_ng_5G_S_TMSI_Part1 tmsiVariant;
        tmsiVariant.ng_5G_S_TMSI_Part1.bit_length = 39;
        for (int i = 0; i < 5; ++i)
            tmsiVariant.ng_5G_S_TMSI_Part1.data.push_back(0xAA);

        NR_RRC_Definitions::RRCSetupRequest_IEs ies;
        ies.ue_Identity        = tmsiVariant;
        ies.establishmentCause = NR_RRC_Definitions::EstablishmentCause::mo_Signalling;
        // spare: BIT STRING (SIZE(1)) — 1 bit, zero
        ies.spare.bit_length = 1;
        ies.spare.data = {0x00};
        req.rrcSetupRequest = ies;

        BitWriter bw;
        NR_RRC_Definitions::encode_RRCSetupRequest(bw, req);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_RRCSetupRequest(br);
        (void)out; // structural check: no exception means decode succeeded
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// 6.  VarResumeMAC_Input  (same fields as VarShortMAC_Input — parallel tests)
// ═══════════════════════════════════════════════════════════════════════════

static void test_VarResumeMAC_roundtrip() {
    expect_ok("VarResumeMAC_Input: valid round-trip", []() {
        NR_UE_Variables::VarResumeMAC_Input input;
        input.sourcePhysCellId   = 512;
        input.source_c_RNTI      = 32768;
        input.targetCellIdentity = makeCellIdentity(0x1FFFFFFFFULL & 0xFFFFFFFFFULL);

        BitWriter bw;
        NR_UE_Variables::encode_VarResumeMAC_Input(bw, input);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_UE_Variables::decode_VarResumeMAC_Input(br);

        assert(out.sourcePhysCellId == 512);
        assert(out.source_c_RNTI    == 32768);
        assert(out.targetCellIdentity.bit_length == 36);
    });
}

static void test_VarResumeMAC_encode_cellIdentity_wrong_size() {
    expect_throw(
        "VarResumeMAC_Input encode: CellIdentity bit_length=32 violates SIZE(36)",
        "BIT STRING SIZE constraint violation",
        []() {
            NR_UE_Variables::VarResumeMAC_Input input;
            input.sourcePhysCellId   = 10;
            input.source_c_RNTI      = 10;
            input.targetCellIdentity.bit_length = 32;
            input.targetCellIdentity.data = {0x00, 0x00, 0x00, 0x00};

            BitWriter bw;
            NR_UE_Variables::encode_VarResumeMAC_Input(bw, input);
        });
}

// ═══════════════════════════════════════════════════════════════════════════
// 7.  Bitstream integrity: encode produces deterministic bytes
// ═══════════════════════════════════════════════════════════════════════════

static void test_deterministic_encoding() {
    expect_ok("Deterministic encoding: same input → same bytes", []() {
        auto make_input = []() {
            NR_UE_Variables::VarShortMAC_Input inp;
            inp.sourcePhysCellId   = 100;
            inp.source_c_RNTI      = 1234;
            inp.targetCellIdentity = makeCellIdentity(0xDEADBEEFULL & 0xFFFFFFFFFULL);
            return inp;
        };

        BitWriter bw1, bw2;
        NR_UE_Variables::encode_VarShortMAC_Input(bw1, make_input());
        NR_UE_Variables::encode_VarShortMAC_Input(bw2, make_input());

        assert(bw1.getBufferSize() == bw2.getBufferSize());
        for (size_t i = 0; i < bw1.getBufferSize(); ++i)
            assert(bw1.getBuffer()[i] == bw2.getBuffer()[i]);
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// 8.  ENUMERATED round-trips  (ReestablishmentCause — no extension marker)
//     ASN: ENUMERATED {reconfigurationFailure, handoverFailure,
//                       otherFailure, spare1}
// ═══════════════════════════════════════════════════════════════════════════

static void test_ReestablishmentCause_all_values() {
    using RC = NR_RRC_Definitions::ReestablishmentCause;
    const RC values[] = {
        RC::reconfigurationFailure,
        RC::handoverFailure,
        RC::otherFailure,
        RC::spare1,
    };
    const char* names[] = {
        "reconfigurationFailure", "handoverFailure",
        "otherFailure", "spare1",
    };
    for (int i = 0; i < 4; ++i) {
        expect_ok(std::string("ReestablishmentCause round-trip: ") + names[i], [&, i]() {
            BitWriter bw;
            NR_RRC_Definitions::encode_ReestablishmentCause(bw, values[i]);
            BitReader br(bw.getBuffer(), bw.getBufferSize());
            [[maybe_unused]] auto out = NR_RRC_Definitions::decode_ReestablishmentCause(br);
            assert(out == values[i]);
        });
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 9.  Nested SEQUENCE  (RRCReestablishmentRequest)
//     SEQUENCE → SEQUENCE { RNTI-Value, PhysCellId, ShortMAC-I(SIZE 16) }
//                         + ReestablishmentCause  + spare BIT STRING(SIZE 1)
// ═══════════════════════════════════════════════════════════════════════════

static void test_RRCReestablishmentRequest_roundtrip() {
    expect_ok("RRCReestablishmentRequest: valid round-trip (nested SEQUENCE + ENUMERATED)", []() {
        NR_RRC_Definitions::ReestabUE_Identity uid;
        uid.c_RNTI     = 1000;
        uid.physCellId = 500;
        uid.shortMAC_I.bit_length = 16;
        uid.shortMAC_I.data = {0xAB, 0xCD};

        NR_RRC_Definitions::RRCReestablishmentRequest_IEs ies;
        ies.ue_Identity          = uid;
        ies.reestablishmentCause = NR_RRC_Definitions::ReestablishmentCause::handoverFailure;
        ies.spare.bit_length = 1;
        ies.spare.data = {0x00};

        NR_RRC_Definitions::RRCReestablishmentRequest req;
        req.rrcReestablishmentRequest = ies;

        BitWriter bw;
        NR_RRC_Definitions::encode_RRCReestablishmentRequest(bw, req);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_RRCReestablishmentRequest(br);

        assert(out.rrcReestablishmentRequest.ue_Identity.c_RNTI     == 1000);
        assert(out.rrcReestablishmentRequest.ue_Identity.physCellId == 500);
        assert(out.rrcReestablishmentRequest.reestablishmentCause   ==
               NR_RRC_Definitions::ReestablishmentCause::handoverFailure);
    });
}

static void test_RRCReestablishmentRequest_constraint_violations() {
    expect_throw(
        "RRCReestablishmentRequest: physCellId=1008 in nested SEQUENCE",
        "INTEGER constraint violation",
        []() {
            NR_RRC_Definitions::ReestabUE_Identity uid;
            uid.c_RNTI     = 0;
            uid.physCellId = 1008; // out of [0..1007]
            uid.shortMAC_I.bit_length = 16;
            uid.shortMAC_I.data = {0x00, 0x00};

            NR_RRC_Definitions::RRCReestablishmentRequest_IEs ies;
            ies.ue_Identity          = uid;
            ies.reestablishmentCause = NR_RRC_Definitions::ReestablishmentCause::spare1;
            ies.spare.bit_length = 1;
            ies.spare.data = {0x00};

            NR_RRC_Definitions::RRCReestablishmentRequest req;
            req.rrcReestablishmentRequest = ies;

            BitWriter bw;
            NR_RRC_Definitions::encode_RRCReestablishmentRequest(bw, req);
        });

    expect_throw(
        "RRCReestablishmentRequest: shortMAC-I bit_length=8 violates SIZE(16)",
        "BIT STRING SIZE constraint violation",
        []() {
            NR_RRC_Definitions::ReestabUE_Identity uid;
            uid.c_RNTI     = 0;
            uid.physCellId = 0;
            uid.shortMAC_I.bit_length = 8; // wrong — must be 16
            uid.shortMAC_I.data = {0x00};

            NR_RRC_Definitions::RRCReestablishmentRequest_IEs ies;
            ies.ue_Identity          = uid;
            ies.reestablishmentCause = NR_RRC_Definitions::ReestablishmentCause::spare1;
            ies.spare.bit_length = 1;
            ies.spare.data = {0x00};

            NR_RRC_Definitions::RRCReestablishmentRequest req;
            req.rrcReestablishmentRequest = ies;

            BitWriter bw;
            NR_RRC_Definitions::encode_RRCReestablishmentRequest(bw, req);
        });
}

// ═══════════════════════════════════════════════════════════════════════════
// 10. BOOLEAN field  (MasterKeyUpdate)
//     SEQUENCE { keySetChangeIndicator BOOLEAN,
//                nextHopChainingCount  INTEGER(0..7),
//                nas-Container         OCTET STRING OPTIONAL,
//                ... }
// ═══════════════════════════════════════════════════════════════════════════

static void test_MasterKeyUpdate_roundtrip() {
    expect_ok("MasterKeyUpdate: round-trip (BOOLEAN true, optional octet string present)", []() {
        NR_RRC_Definitions::MasterKeyUpdate ku;
        ku.keySetChangeIndicator = true;
        ku.nextHopChainingCount  = 3;
        ku.nas_Container         = std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF};

        BitWriter bw;
        NR_RRC_Definitions::encode_MasterKeyUpdate(bw, ku);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_MasterKeyUpdate(br);

        assert(out.keySetChangeIndicator == true);
        assert(out.nextHopChainingCount  == 3);
        assert(out.nas_Container.has_value());
        assert(out.nas_Container.value() == ku.nas_Container.value());
    });

    expect_ok("MasterKeyUpdate: round-trip (BOOLEAN false, optional absent)", []() {
        NR_RRC_Definitions::MasterKeyUpdate ku;
        ku.keySetChangeIndicator = false;
        ku.nextHopChainingCount  = 0;
        // nas_Container absent

        BitWriter bw;
        NR_RRC_Definitions::encode_MasterKeyUpdate(bw, ku);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_MasterKeyUpdate(br);

        assert(out.keySetChangeIndicator == false);
        assert(out.nextHopChainingCount  == 0);
        assert(!out.nas_Container.has_value());
    });
}

static void test_MasterKeyUpdate_integer_constraint() {
    expect_throw(
        "MasterKeyUpdate: nextHopChainingCount=8 violates [0..7]",
        "INTEGER constraint violation",
        []() {
            NR_RRC_Definitions::MasterKeyUpdate ku;
            ku.keySetChangeIndicator = false;
            ku.nextHopChainingCount  = 8; // out of [0..7]

            BitWriter bw;
            NR_RRC_Definitions::encode_MasterKeyUpdate(bw, ku);
        });
}

// ═══════════════════════════════════════════════════════════════════════════
// 11. SEQUENCE OF with SIZE constraint
//
//   MCC ::= SEQUENCE (SIZE (3))    OF MCC-MNC-Digit  (INTEGER 0..9)
//   MNC ::= SEQUENCE (SIZE (2..3)) OF MCC-MNC-Digit
//   DRB-CountMSB-InfoList ::= SEQUENCE (SIZE (1..29)) OF DRB-CountMSB-Info
// ═══════════════════════════════════════════════════════════════════════════

static void test_MCC_MNC_roundtrip() {
    expect_ok("PLMN-Identity round-trip (MCC present, MNC 3 digits)", []() {
        NR_RRC_Definitions::PLMN_Identity plmn;
        plmn.mcc = NR_RRC_Definitions::MCC{2, 0, 4}; // UK MCC
        plmn.mnc = NR_RRC_Definitions::MNC{1, 5};    // 2-digit MNC

        BitWriter bw;
        NR_RRC_Definitions::encode_PLMN_Identity(bw, plmn);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_PLMN_Identity(br);

        assert(out.mcc.has_value());
        assert(out.mcc.value().size() == 3);
        assert(out.mcc.value()[0] == 2);
        assert(out.mcc.value()[1] == 0);
        assert(out.mcc.value()[2] == 4);
        assert(out.mnc.size() == 2);
        assert(out.mnc[0] == 1);
        assert(out.mnc[1] == 5);
    });

    expect_ok("PLMN-Identity round-trip (MCC absent, MNC 3 digits)", []() {
        NR_RRC_Definitions::PLMN_Identity plmn;
        // mcc absent
        plmn.mnc = NR_RRC_Definitions::MNC{3, 1, 0}; // 3-digit MNC

        BitWriter bw;
        NR_RRC_Definitions::encode_PLMN_Identity(bw, plmn);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_PLMN_Identity(br);

        assert(!out.mcc.has_value());
        assert(out.mnc.size() == 3);
        assert(out.mnc[0] == 3 && out.mnc[1] == 1 && out.mnc[2] == 0);
    });
}

static void test_MCC_fixed_size_violation() {
    expect_throw(
        "MCC encode: 2 digits violates SIZE(3)",
        "SEQUENCE OF SIZE constraint violation",
        []() {
            NR_RRC_Definitions::MCC mcc{2, 0}; // only 2 digits, needs 3
            BitWriter bw;
            NR_RRC_Definitions::encode_MCC(bw, mcc);
        });

    expect_throw(
        "MCC encode: 4 digits violates SIZE(3)",
        "SEQUENCE OF SIZE constraint violation",
        []() {
            NR_RRC_Definitions::MCC mcc{2, 0, 4, 0}; // 4 digits, needs exactly 3
            BitWriter bw;
            NR_RRC_Definitions::encode_MCC(bw, mcc);
        });
}

static void test_MNC_range_size_violation() {
    expect_throw(
        "MNC encode: 1 digit violates SIZE(2..3)",
        "SEQUENCE OF SIZE constraint violation",
        []() {
            NR_RRC_Definitions::MNC mnc{5}; // only 1 digit, needs 2..3
            BitWriter bw;
            NR_RRC_Definitions::encode_MNC(bw, mnc);
        });

    expect_throw(
        "MNC encode: 4 digits violates SIZE(2..3)",
        "SEQUENCE OF SIZE constraint violation",
        []() {
            NR_RRC_Definitions::MNC mnc{1, 2, 3, 4}; // 4 digits, needs 2..3
            BitWriter bw;
            NR_RRC_Definitions::encode_MNC(bw, mnc);
        });
}

static void test_DRB_CountMSB_InfoList_constraint() {
    // Empty list violates SIZE(1..29)
    expect_throw(
        "DRB-CountMSB-InfoList encode: empty list violates SIZE(1..29)",
        "SEQUENCE OF SIZE constraint violation",
        []() {
            NR_RRC_Definitions::DRB_CountMSB_InfoList list; // empty
            BitWriter bw;
            NR_RRC_Definitions::encode_DRB_CountMSB_InfoList(bw, list);
        });

    // 30 elements violates SIZE(1..29)
    expect_throw(
        "DRB-CountMSB-InfoList encode: 30 elements violates SIZE(1..29)",
        "SEQUENCE OF SIZE constraint violation",
        []() {
            NR_RRC_Definitions::DRB_CountMSB_InfoList list;
            for (int i = 0; i < 30; ++i) {
                NR_RRC_Definitions::DRB_CountMSB_Info info;
                info.drb_Identity       = 1; // valid DRB-Identity [1..32]
                info.countMSB_Uplink    = 0;
                info.countMSB_Downlink  = 0;
                list.push_back(info);
            }
            BitWriter bw;
            NR_RRC_Definitions::encode_DRB_CountMSB_InfoList(bw, list);
        });

    // Valid: 1 element
    expect_ok("DRB-CountMSB-InfoList: round-trip with 1 element (min SIZE)", []() {
        NR_RRC_Definitions::DRB_CountMSB_InfoList list;
        NR_RRC_Definitions::DRB_CountMSB_Info info;
        info.drb_Identity      = 1;
        info.countMSB_Uplink   = 33554431; // max value (2^25 - 1)
        info.countMSB_Downlink = 0;
        list.push_back(info);

        BitWriter bw;
        NR_RRC_Definitions::encode_DRB_CountMSB_InfoList(bw, list);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_DRB_CountMSB_InfoList(br);

        assert(out.size() == 1);
        assert(out[0].drb_Identity      == 1);
        assert(out[0].countMSB_Uplink   == 33554431);
        assert(out[0].countMSB_Downlink == 0);
    });
}

static void test_DRB_Identity_constraint() {
    // DRB-Identity ::= INTEGER (1..32)
    expect_throw(
        "DRB-Identity encode: 0 violates [1..32]",
        "INTEGER constraint violation",
        []() {
            NR_RRC_Definitions::DRB_CountMSB_InfoList list;
            NR_RRC_Definitions::DRB_CountMSB_Info info;
            info.drb_Identity      = 0; // below min of 1
            info.countMSB_Uplink   = 0;
            info.countMSB_Downlink = 0;
            list.push_back(info);
            BitWriter bw;
            NR_RRC_Definitions::encode_DRB_CountMSB_InfoList(bw, list);
        });

    expect_throw(
        "DRB-Identity encode: 33 violates [1..32]",
        "INTEGER constraint violation",
        []() {
            NR_RRC_Definitions::DRB_CountMSB_InfoList list;
            NR_RRC_Definitions::DRB_CountMSB_Info info;
            info.drb_Identity      = 33; // above max of 32
            info.countMSB_Uplink   = 0;
            info.countMSB_Downlink = 0;
            list.push_back(info);
            BitWriter bw;
            NR_RRC_Definitions::encode_DRB_CountMSB_InfoList(bw, list);
        });
}

// ═══════════════════════════════════════════════════════════════════════════
// 12. CHOICE with multiple alternatives  (PagingUE-Identity)
//     CHOICE { ng-5G-S-TMSI BIT STRING(SIZE 48),
//              fullI-RNTI   BIT STRING(SIZE 40),
//              ... }
// ═══════════════════════════════════════════════════════════════════════════

static void test_PagingUE_Identity_ng5g_roundtrip() {
    expect_ok("PagingUE-Identity: round-trip (ng-5G-S-TMSI, 48 bits)", []() {
        NR_RRC_Definitions::PagingUE_Identity_ng_5G_S_TMSI alt;
        alt.ng_5G_S_TMSI.bit_length = 48;
        alt.ng_5G_S_TMSI.data = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

        BitWriter bw;
        NR_RRC_Definitions::encode_PagingUE_Identity(bw,
            NR_RRC_Definitions::PagingUE_Identity{alt});
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_PagingUE_Identity(br);

        assert(std::holds_alternative<
               NR_RRC_Definitions::PagingUE_Identity_ng_5G_S_TMSI>(out));
        [[maybe_unused]] const auto& got = std::get<
            NR_RRC_Definitions::PagingUE_Identity_ng_5G_S_TMSI>(out);
        assert(got.ng_5G_S_TMSI.bit_length == 48);
        assert(got.ng_5G_S_TMSI.data == alt.ng_5G_S_TMSI.data);
    });
}

static void test_PagingUE_Identity_irnti_roundtrip() {
    expect_ok("PagingUE-Identity: round-trip (fullI-RNTI, 40 bits)", []() {
        NR_RRC_Definitions::PagingUE_Identity_fullI_RNTI alt;
        alt.fullI_RNTI.bit_length = 40;
        alt.fullI_RNTI.data = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

        BitWriter bw;
        NR_RRC_Definitions::encode_PagingUE_Identity(bw,
            NR_RRC_Definitions::PagingUE_Identity{alt});
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_PagingUE_Identity(br);

        assert(std::holds_alternative<
               NR_RRC_Definitions::PagingUE_Identity_fullI_RNTI>(out));
        [[maybe_unused]] const auto& gotr = std::get<
            NR_RRC_Definitions::PagingUE_Identity_fullI_RNTI>(out);
        assert(gotr.fullI_RNTI.bit_length == 40);
        assert(gotr.fullI_RNTI.data == alt.fullI_RNTI.data);
    });
}

static void test_PagingUE_Identity_bitstring_size_violation() {
    expect_throw(
        "PagingUE-Identity ng-5G-S-TMSI: bit_length=32 violates SIZE(48)",
        "BIT STRING SIZE constraint violation",
        []() {
            NR_RRC_Definitions::PagingUE_Identity_ng_5G_S_TMSI alt;
            alt.ng_5G_S_TMSI.bit_length = 32; // wrong, must be 48
            alt.ng_5G_S_TMSI.data = {0x00, 0x00, 0x00, 0x00};

            BitWriter bw;
            NR_RRC_Definitions::encode_PagingUE_Identity(bw,
                NR_RRC_Definitions::PagingUE_Identity{alt});
        });
}

// ═══════════════════════════════════════════════════════════════════════════
// 13. PagingRecord  (SEQUENCE with optional ENUMERATED, extension marker)
// ═══════════════════════════════════════════════════════════════════════════

static void test_PagingRecord_roundtrip() {
    expect_ok("PagingRecord: round-trip (ng-5G-S-TMSI identity, accessType present)", []() {
        NR_RRC_Definitions::PagingUE_Identity_ng_5G_S_TMSI tmsi;
        tmsi.ng_5G_S_TMSI.bit_length = 48;
        tmsi.ng_5G_S_TMSI.data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};

        NR_RRC_Definitions::PagingRecord rec;
        rec.ue_Identity = NR_RRC_Definitions::PagingUE_Identity{tmsi};
        rec.accessType  = NR_RRC_Definitions::PagingRecord::accessType_type::non3GPP;

        BitWriter bw;
        NR_RRC_Definitions::encode_PagingRecord(bw, rec);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_PagingRecord(br);

        assert(std::holds_alternative<
               NR_RRC_Definitions::PagingUE_Identity_ng_5G_S_TMSI>(out.ue_Identity));
        assert(out.accessType.has_value());
        assert(out.accessType.value() ==
               NR_RRC_Definitions::PagingRecord::accessType_type::non3GPP);
    });

    expect_ok("PagingRecord: round-trip (accessType absent)", []() {
        NR_RRC_Definitions::PagingUE_Identity_fullI_RNTI irnti;
        irnti.fullI_RNTI.bit_length = 40;
        irnti.fullI_RNTI.data = {0x11, 0x22, 0x33, 0x44, 0x55};

        NR_RRC_Definitions::PagingRecord rec;
        rec.ue_Identity = NR_RRC_Definitions::PagingUE_Identity{irnti};
        // accessType absent

        BitWriter bw;
        NR_RRC_Definitions::encode_PagingRecord(bw, rec);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_RRC_Definitions::decode_PagingRecord(br);

        assert(!out.accessType.has_value());
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// 14. Cross-module references  (NR_InterNodeDefinitions types)
//     MeasurementTimingConfiguration → criticalExtensionsFuture path
//     (simplest valid encoding: no measTiming list)
// ═══════════════════════════════════════════════════════════════════════════

static void test_MeasurementTimingConfiguration_roundtrip() {
    expect_ok("MeasurementTimingConfiguration: round-trip (empty measTiming list)", []() {
        NR_InterNodeDefinitions::MeasurementTimingConfiguration_IEs ies;
        // measTiming absent
        // nonCriticalExtension absent

        NR_InterNodeDefinitions::MeasurementTimingConfiguration::criticalExtensions_c1_type_measTimingConf c1val;
        c1val.measTimingConf = ies;

        NR_InterNodeDefinitions::MeasurementTimingConfiguration::criticalExtensions_c1 c1;
        c1.c1 = c1val;

        NR_InterNodeDefinitions::MeasurementTimingConfiguration msg;
        msg.criticalExtensions = c1;

        BitWriter bw;
        NR_InterNodeDefinitions::encode_MeasurementTimingConfiguration(bw, msg);
        BitReader br(bw.getBuffer(), bw.getBufferSize());
        auto out = NR_InterNodeDefinitions::decode_MeasurementTimingConfiguration(br);
        (void)out; // structural check
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// entry point
// ═══════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "=== nr-rrc-15.6.0 schema integration tests ===\n\n";

    // VarShortMAC_Input
    test_VarShortMAC_roundtrip();
    test_VarShortMAC_encode_cellIdentity_wrong_size();
    test_VarShortMAC_encode_physCellId_too_high();
    test_VarShortMAC_encode_physCellId_negative();
    test_VarShortMAC_encode_rnti_too_high();
    test_VarShortMAC_encode_boundary_values();

    // PhysCellId
    test_PhysCellId_roundtrip();
    test_PhysCellId_roundtrip_boundaries();
    test_PhysCellId_encode_violation();

    // CellIdentity
    test_CellIdentity_roundtrip();
    test_CellIdentity_encode_wrong_size();

    // VarPendingRNA_Update
    test_VarPendingRNA_roundtrip_absent();
    test_VarPendingRNA_roundtrip_true();
    test_VarPendingRNA_roundtrip_false();

    // RRCSetupRequest
    test_RRCSetupRequest_roundtrip();

    // VarResumeMAC_Input
    test_VarResumeMAC_roundtrip();
    test_VarResumeMAC_encode_cellIdentity_wrong_size();

    // Bitstream integrity
    test_deterministic_encoding();

    // ENUMERATED round-trips
    test_ReestablishmentCause_all_values();

    // Nested SEQUENCE + ENUMERATED + BIT STRING (RRCReestablishmentRequest)
    test_RRCReestablishmentRequest_roundtrip();
    test_RRCReestablishmentRequest_constraint_violations();

    // BOOLEAN field + INTEGER constraint (MasterKeyUpdate)
    test_MasterKeyUpdate_roundtrip();
    test_MasterKeyUpdate_integer_constraint();

    // SEQUENCE OF with fixed and ranged SIZE constraints
    test_MCC_MNC_roundtrip();
    test_MCC_fixed_size_violation();
    test_MNC_range_size_violation();
    test_DRB_CountMSB_InfoList_constraint();
    test_DRB_Identity_constraint();

    // CHOICE with multiple alternatives (PagingUE-Identity)
    test_PagingUE_Identity_ng5g_roundtrip();
    test_PagingUE_Identity_irnti_roundtrip();
    test_PagingUE_Identity_bitstring_size_violation();

    // SEQUENCE with optional ENUMERATED + extension marker (PagingRecord)
    test_PagingRecord_roundtrip();

    // Cross-module references (NR_InterNodeDefinitions)
    test_MeasurementTimingConfiguration_roundtrip();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}
