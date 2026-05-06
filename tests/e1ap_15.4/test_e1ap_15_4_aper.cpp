// APER integration tests for e1ap_15.4.asn1
// Each TC matches a real Wireshark capture of an E1AP message.
//
// TC1 – E1AP-PDU wrapping BearerContextModificationRequest (3 IEs)
// TC2 – BearerContextModificationRequest decoded directly (same payload as TC1 inner)
// TC3 – E1AP-PDU wrapping BearerContextSetupRequest (8 IEs)
// TC4 – BearerContextSetupResponse decoded directly (3 IEs)

#include "e1ap_15_4.h"

#include <any>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace asn1::generated;
using namespace asn1::runtime;
namespace IEs  = asn1::generated::E1AP_IEs;
namespace PDU  = asn1::generated::E1AP_PDU_Descriptions;
namespace Cont = asn1::generated::E1AP_PDU_Contents;
using IE = asn1::generated::E1AP_Containers::ProtocolIE_Field;

// ─── bookkeeping ─────────────────────────────────────────────────────────────

static int passed = 0;
static int failed = 0;

static void report_ok(const std::string& name) {
    std::cout << "[PASS] " << name << "\n";
    ++passed;
}

static void report_fail(const std::string& name, const std::string& reason) {
    std::cout << "[FAIL] " << name << " : " << reason << "\n";
    ++failed;
}

template <typename F>
static void tc(const std::string& name, F&& f) {
    try { f(); report_ok(name); }
    catch (const std::exception& e) { report_fail(name, e.what()); }
}

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::vector<uint8_t> from_hex(const std::string& h) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < h.size(); i += 2)
        v.push_back(static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
    return v;
}

static std::string to_hex(const std::vector<uint8_t>& v) {
    std::ostringstream s;
    for (auto b : v) s << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return s.str();
}

static void chk(bool ok, const std::string& msg) {
    if (!ok) throw std::runtime_error(msg);
}

template <typename T>
static void chk_eq(const std::string& field, T got, T want) {
    if (got != want)
        throw std::runtime_error(field + ": expected " + std::to_string((long long)want)
                                 + ", got " + std::to_string((long long)got));
}

// Verify IE id and criticality; criticality as int (reject=0, ignore=1, notify=2).
static void chk_ie(const IE& ie, int64_t id, int crit) {
    chk_eq("IE.id", (int64_t)ie.id, id);
    chk_eq("IE[id=" + std::to_string(id) + "].criticality", (int)ie.criticality, crit);
}

// Get the value BitString from an IE; throw if not present.
static const BitString& ie_bs(const IE& ie) {
    const auto* p = std::any_cast<BitString>(&ie.value);
    if (!p) throw std::runtime_error("IE value is not BitString");
    return *p;
}

// Decode GNB-CU-CP-UE-E1AP-ID from an IE's open-type bytes.
static int64_t decode_gnb_cp(const IE& ie) {
    const auto& bs = ie_bs(ie);
    BitReader r(bs.data.data(), bs.bit_length);
    return IEs::decode_GNB_CU_CP_UE_E1AP_ID(r);
}

static int64_t decode_gnb_up(const IE& ie) {
    const auto& bs = ie_bs(ie);
    BitReader r(bs.data.data(), bs.bit_length);
    return IEs::decode_GNB_CU_UP_UE_E1AP_ID(r);
}

// ─── TC1: E1AP-PDU → BearerContextModificationRequest ───────────────────────
//
// procedureCode=9, criticality=reject, 3 IEs:
//   [0] id=2  (gNB-CU-CP-UE-E1AP-ID)  crit=reject  value=50921473
//   [1] id=3  (gNB-CU-UP-UE-E1AP-ID)  crit=reject  value=50921473
//   [2] id=18 (System-BearerCtxModReq) crit=reject  (open type, not decoded)

static void tc1_full_pdu_bearer_ctx_mod_req() {
    const auto bytes = from_hex(
        "0009003400000300020005c00309000100030005c003090001"
        "0012001b400001002b00140000400502100000000f80c0a809020000000100");

    BitReader reader(bytes.data(), bytes.size() * 8);
    auto pdu = PDU::decode_E1AP_PDU(reader);

    chk(pdu.index() == 0, "E1AP-PDU variant index must be 0 (initiatingMessage)");
    auto& init = std::get<0>(pdu).initiatingMessage;

    chk_eq("procedureCode", (int64_t)init.procedureCode, (int64_t)9);
    chk_eq("criticality",   (int)init.criticality,       0); // reject

    // decode inner BearerContextModificationRequest
    const auto& outer_bs = std::any_cast<const BitString&>(init.value);
    BitReader inner(outer_bs.data.data(), outer_bs.bit_length);
    auto req = Cont::decode_BearerContextModificationRequest(inner);

    auto& ies = req.protocolIEs;
    chk_eq("numIEs", (int)ies.size(), 3);

    chk_ie(ies[0], 2, 0);
    chk_eq("IE[0] gNB-CU-CP-UE-E1AP-ID", decode_gnb_cp(ies[0]), (int64_t)50921473);

    chk_ie(ies[1], 3, 0);
    chk_eq("IE[1] gNB-CU-UP-UE-E1AP-ID", decode_gnb_up(ies[1]), (int64_t)50921473);

    chk_ie(ies[2], 18, 0); // id-System-BearerContextModificationRequest
}

// ─── TC2: BearerContextModificationRequest (no PDU wrapper) ─────────────────
//
// Same 3 IEs as TC1 inner payload.

static void tc2_bearer_ctx_mod_req_direct() {
    const auto bytes = from_hex(
        "00000300020005c00309000100030005c003090001"
        "0012001b400001002b00140000400502100000000f80c0a809020000000100");

    BitReader reader(bytes.data(), bytes.size() * 8);
    auto req = Cont::decode_BearerContextModificationRequest(reader);

    auto& ies = req.protocolIEs;
    chk_eq("numIEs", (int)ies.size(), 3);

    chk_ie(ies[0], 2, 0);
    chk_eq("IE[0] gNB-CU-CP-UE-E1AP-ID", decode_gnb_cp(ies[0]), (int64_t)50921473);

    chk_ie(ies[1], 3, 0);
    chk_eq("IE[1] gNB-CU-UP-UE-E1AP-ID", decode_gnb_up(ies[1]), (int64_t)50921473);

    chk_ie(ies[2], 18, 0);

    // IE[2] value is System-BearerContextModificationRequest with nG-RAN choice.
    // Verify the raw open-type bytes match the expected hex.
    const auto& bs2 = ie_bs(ies[2]);
    chk(bs2.bit_length > 0, "IE[2] value must be non-empty");
    const std::string expected_hex =
        "400001002b00140000400502100000000f80c0a809020000000100";
    chk(to_hex(bs2.data).find(expected_hex.substr(0, 16)) == 0,
        "IE[2] value bytes prefix mismatch");
}

// ─── TC3: E1AP-PDU → BearerContextSetupRequest (8 IEs) ──────────────────────
//
// procedureCode=8, criticality=reject, 8 IEs:
//   [0] id=2  gNB-CU-CP-UE-E1AP-ID        crit=reject  value=50921473
//   [1] id=13 SecurityInformation          crit=reject
//   [2] id=14 UEDLAggregateMaximumBitRate  crit=reject  value=2000000000
//   [3] id=58 Serving-PLMN                 crit=ignore
//   [4] id=23 ActivityNotificationLevel    crit=reject  value=ue(2)
//   [5] id=59 UE-Inactivity-Timer          crit=reject  value=1980
//   [6] id=15 System-BearerContextSetupReq crit=reject
//   [7] id=77 GNB-DU-ID                    crit=ignore  value=1

static void tc3_full_pdu_bearer_ctx_setup_req() {
    const auto bytes = from_hex(
        "000800808b00000800020005c003090001000d002410481000000000000000000000000000000000"
        "107f0469be3493e32c2aa7cd77ff2d35da000e00053077359400003a400300f110001700014000"
        "3b00030007bb000f0031400001002a002a0040050402000001002002540be40001f00505050101"
        "000000000010300a40af0000008020090007d060004d40020001");

    BitReader reader(bytes.data(), bytes.size() * 8);
    auto pdu = PDU::decode_E1AP_PDU(reader);

    chk(pdu.index() == 0, "E1AP-PDU must be initiatingMessage");
    auto& init = std::get<0>(pdu).initiatingMessage;

    chk_eq("procedureCode", (int64_t)init.procedureCode, (int64_t)8);
    chk_eq("criticality",   (int)init.criticality,       0); // reject

    const auto& outer_bs = std::any_cast<const BitString&>(init.value);
    BitReader inner(outer_bs.data.data(), outer_bs.bit_length);
    auto req = Cont::decode_BearerContextSetupRequest(inner);

    auto& ies = req.protocolIEs;
    chk_eq("numIEs", (int)ies.size(), 8);

    // IE[0]: gNB-CU-CP-UE-E1AP-ID = 50921473
    chk_ie(ies[0], 2, 0);
    chk_eq("IE[0] gNB-CU-CP-UE-E1AP-ID", decode_gnb_cp(ies[0]), (int64_t)50921473);

    // IE[1]: SecurityInformation (complex struct, just check id/crit)
    chk_ie(ies[1], 13, 0);

    // IE[2]: UEDLAggregateMaximumBitRate = 2000000000
    chk_ie(ies[2], 14, 0);
    {
        const auto& bs = ie_bs(ies[2]);
        BitReader r(bs.data.data(), bs.bit_length);
        auto br = IEs::decode_BitRate(r);
        chk_eq("IE[2] BitRate", br, (int64_t)2000000000);
    }

    // IE[3]: Serving-PLMN = 00f110, criticality=ignore(1)
    chk_ie(ies[3], 58, 1);
    {
        const auto& bs = ie_bs(ies[3]);
        BitReader r(bs.data.data(), bs.bit_length);
        auto plmn = IEs::decode_PLMN_Identity(r);
        chk(plmn.size() == 3, "PLMN-Identity must be 3 bytes");
        chk(plmn[0] == 0x00 && plmn[1] == 0xf1 && plmn[2] == 0x10,
            "PLMN-Identity mismatch: expected 00f110");
    }

    // IE[4]: ActivityNotificationLevel = ue (index 2)
    chk_ie(ies[4], 23, 0);
    {
        const auto& bs = ie_bs(ies[4]);
        BitReader r(bs.data.data(), bs.bit_length);
        auto lvl = IEs::decode_ActivityNotificationLevel(r);
        chk(lvl == IEs::ActivityNotificationLevel::ue,
            "ActivityNotificationLevel must be ue");
    }

    // IE[5]: UE-Inactivity-Timer = 1980
    chk_ie(ies[5], 59, 0);
    {
        const auto& bs = ie_bs(ies[5]);
        BitReader r(bs.data.data(), bs.bit_length);
        auto t = IEs::decode_Inactivity_Timer(r);
        chk_eq("IE[5] Inactivity-Timer", t, (int64_t)1980);
    }

    // IE[6]: System-BearerContextSetupRequest (complex, just id/crit)
    chk_ie(ies[6], 15, 0);

    // IE[7]: GNB-DU-ID = 1, criticality=ignore(1)
    chk_ie(ies[7], 77, 1);
    {
        const auto& bs = ie_bs(ies[7]);
        BitReader r(bs.data.data(), bs.bit_length);
        auto du_id = IEs::decode_GNB_DU_ID(r);
        chk_eq("IE[7] GNB-DU-ID", du_id, (int64_t)1);
    }
}

// ─── TC4: BearerContextSetupResponse (no PDU wrapper, 3 IEs) ─────────────────
//
//   [0] id=2  gNB-CU-CP-UE-E1AP-ID         crit=reject  value=50921473
//   [1] id=3  gNB-CU-UP-UE-E1AP-ID         crit=reject  value=50921473
//   [2] id=16 System-BearerContextSetupResp crit=ignore

static void tc4_bearer_ctx_setup_response_direct() {
    const auto bytes = from_hex(
        "00000300020005c00309000100030005c00309000100104023400001002e401c00000501f0050505"
        "020900010c0000001fc0a809010d000120000080");

    BitReader reader(bytes.data(), bytes.size() * 8);
    auto resp = Cont::decode_BearerContextSetupResponse(reader);

    auto& ies = resp.protocolIEs;
    chk_eq("numIEs", (int)ies.size(), 3);

    chk_ie(ies[0], 2, 0);
    chk_eq("IE[0] gNB-CU-CP-UE-E1AP-ID", decode_gnb_cp(ies[0]), (int64_t)50921473);

    chk_ie(ies[1], 3, 0);
    chk_eq("IE[1] gNB-CU-UP-UE-E1AP-ID", decode_gnb_up(ies[1]), (int64_t)50921473);

    chk_ie(ies[2], 16, 1); // id-System-BearerContextSetupResponse, criticality=ignore
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== E1AP APER integration tests ===\n\n";

    tc("TC1: E1AP-PDU BearerContextModificationRequest (3 IEs)",
       tc1_full_pdu_bearer_ctx_mod_req);

    tc("TC2: BearerContextModificationRequest direct decode (3 IEs)",
       tc2_bearer_ctx_mod_req_direct);

    tc("TC3: E1AP-PDU BearerContextSetupRequest (8 IEs)",
       tc3_full_pdu_bearer_ctx_setup_req);

    tc("TC4: BearerContextSetupResponse direct decode (3 IEs)",
       tc4_bearer_ctx_setup_response_direct);

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}
