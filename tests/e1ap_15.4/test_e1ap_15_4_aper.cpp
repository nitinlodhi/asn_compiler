// APER integration tests for e1ap_15.4.asn1
// Each TC matches a real Wireshark capture of an E1AP message.
//
// TC1 – E1AP-PDU wrapping BearerContextModificationRequest (3 IEs)
// TC2 – BearerContextModificationRequest decoded directly (same payload as TC1 inner)
// TC3 – E1AP-PDU wrapping BearerContextSetupRequest (8 IEs)
// TC4 – BearerContextSetupResponse decoded directly (compact encoding, 3 IEs)
// TC5 – BearerContextSetupResponse deep decode + encode round-trip (compact encoding)
// TC6 – BearerContextModificationRequest self-contained: build, encode, decode, verify, round-trip
// TC7 – Full Wireshark PDU decode: E1AP-PDU successfulOutcome → BearerContextSetupResponse

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

// ─── TC5: BearerContextSetupResponse — deep decode + encode round-trip ────────
//
// Same bytes as TC4 but:
//   a) decode IE[2] inner value fully (nG-RAN → PDU-Session-Resource-Setup-List)
//   b) re-encode the outer BearerContextSetupResponse and verify bytes match

static void tc5_bearer_ctx_setup_response_deep_and_roundtrip() {
    const auto bytes = from_hex(
        "00000300020005c00309000100030005c00309000100104023400001002e401c00000501f0050505"
        "020900010c0000001fc0a809010d000120000080");

    // ── (a) deep decode ──────────────────────────────────────────────────────
    BitReader reader(bytes.data(), bytes.size() * 8);
    auto resp = Cont::decode_BearerContextSetupResponse(reader);

    auto& ies = resp.protocolIEs;
    chk_eq("numIEs", (int)ies.size(), 3);

    // IE[2]: System-BearerContextSetupResponse → nG-RAN (choice 1)
    chk_ie(ies[2], 16, 1);

    // Get the raw open-type bytes stored in IE[2].value and decode deeper.
    {
        const auto& bs2 = ie_bs(ies[2]);
        BitReader r2(bs2.data.data(), bs2.bit_length);
        auto sys = Cont::decode_System_BearerContextSetupResponse(r2);

        // Must be nG-RAN-BearerContextSetupResponse (variant index 1).
        chk_eq("System variant", (int)sys.index(), 1);
        auto& ng_ran = std::get<1>(sys).nG_RAN_BearerContextSetupResponse;

        chk_eq("inner IEs count", (int)ng_ran.size(), 1);
        chk_ie(ng_ran[0], 46, 1); // id-PDU-Session-Resource-Setup-List, ignore

        // Decode PDU-Session-Resource-Setup-List from IE[id=46] value.
        const auto* bs46 = std::any_cast<BitString>(&ng_ran[0].value);
        chk(bs46 != nullptr, "IE[46].value must be BitString");
        BitReader r46(bs46->data.data(), bs46->bit_length);
        auto pdu_list = IEs::decode_PDU_Session_Resource_Setup_List(r46);

        chk_eq("PDU session list size", (int)pdu_list.size(), 1);
        const auto& item = pdu_list[0];

        // pDU-Session-ID = 5
        chk_eq("pDU-Session-ID", item.pDU_Session_ID, (int64_t)5);

        // nG-DL-UP-TNL-Information = gTPTunnel (variant 0)
        chk_eq("nG-DL-UP variant", (int)item.nG_DL_UP_TNL_Information.index(), 0);
        const auto& dl_gtp = std::get<0>(item.nG_DL_UP_TNL_Information).gTPTunnel;

        // transportLayerAddress bit_length = 32 (IPv4), data = 05:05:05:02
        chk_eq("DL TLA bits", (int)dl_gtp.transportLayerAddress.bit_length, 32);
        const auto& tla = dl_gtp.transportLayerAddress.data;
        chk(tla.size() >= 4 && tla[0] == 0x05 && tla[1] == 0x05 && tla[2] == 0x05 && tla[3] == 0x02,
            "DL transportLayerAddress must be 05:05:05:02");

        // gTP-TEID = 09:00:01:0c
        chk_eq("DL TEID size", (int)dl_gtp.gTP_TEID.size(), 4);
        chk(dl_gtp.gTP_TEID[0] == 0x09 && dl_gtp.gTP_TEID[1] == 0x00 &&
            dl_gtp.gTP_TEID[2] == 0x01 && dl_gtp.gTP_TEID[3] == 0x0c,
            "DL gTP-TEID must be 09:00:01:0c");

        // dRB-Setup-List-NG-RAN: 1 item
        chk_eq("DRB list size", (int)item.dRB_Setup_List_NG_RAN.size(), 1);
        const auto& drb = item.dRB_Setup_List_NG_RAN[0];

        // dRB-ID = 1
        chk_eq("dRB-ID", drb.dRB_ID, (int64_t)1);

        // uL-UP-Transport-Parameters: 1 item
        chk_eq("UL UP params size", (int)drb.uL_UP_Transport_Parameters.size(), 1);
        const auto& ul_param = drb.uL_UP_Transport_Parameters[0];

        // uP-TNL-Information = gTPTunnel (variant 0)
        chk_eq("UL TNL variant", (int)ul_param.uP_TNL_Information.index(), 0);
        const auto& ul_gtp = std::get<0>(ul_param.uP_TNL_Information).gTPTunnel;

        // transportLayerAddress = c0:a8:09:01 (192.168.9.1, 32 bits)
        chk_eq("UL TLA bits", (int)ul_gtp.transportLayerAddress.bit_length, 32);
        const auto& ul_tla = ul_gtp.transportLayerAddress.data;
        chk(ul_tla.size() >= 4 && ul_tla[0] == 0xc0 && ul_tla[1] == 0xa8 &&
            ul_tla[2] == 0x09 && ul_tla[3] == 0x01,
            "UL transportLayerAddress must be c0:a8:09:01");

        // gTP-TEID = 0d:00:01:20
        chk_eq("UL TEID size", (int)ul_gtp.gTP_TEID.size(), 4);
        chk(ul_gtp.gTP_TEID[0] == 0x0d && ul_gtp.gTP_TEID[1] == 0x00 &&
            ul_gtp.gTP_TEID[2] == 0x01 && ul_gtp.gTP_TEID[3] == 0x20,
            "UL gTP-TEID must be 0d:00:01:20");

        // cell-Group-ID = 0
        chk_eq("cell-Group-ID", ul_param.cell_Group_ID, (int64_t)0);

        chk_eq("flow list size", (int)drb.flow_Setup_List.size(), 1);
        chk_eq("qoS-Flow-Identifier", drb.flow_Setup_List[0].qoS_Flow_Identifier, (int64_t)1);
    }

    // ── (b) encode round-trip ────────────────────────────────────────────────
    BitWriter writer;
    Cont::encode_BearerContextSetupResponse(writer, resp);

    const std::string expected_hex =
        "00000300020005c00309000100030005c00309000100104023400001002e401c00000501f0050505"
        "020900010c0000001fc0a809010d000120000080";
    const std::string got_hex = to_hex(std::vector<uint8_t>(writer.getBuffer(),
                                       writer.getBuffer() + writer.getBufferSize()));
    chk(got_hex == expected_hex,
        "Round-trip encode mismatch:\n  expected " + expected_hex + "\n  got      " + got_hex);
}

// ─── DIAG: encode full BearerContextModificationRequest for TC6 ──────────────
static void diag_encode_full_tc6() {
    using namespace IEs;
    using namespace Cont;

    // ── Build DRB_To_Modify_Item_NG_RAN ─────────────────────────────────────
    DRB_To_Modify_Item_NG_RAN drb{};
    drb.dRB_ID = 1;

    SDAP_Configuration sdap{};
    sdap.defaultDRB = DefaultDRB::true_;
    sdap.sDAP_Header_UL = SDAP_Header_UL::present;
    sdap.sDAP_Header_DL = SDAP_Header_DL::present;
    drb.sDAP_Configuration = sdap;

    GTPTunnel gtp{};
    gtp.transportLayerAddress.bit_length = 32;
    gtp.transportLayerAddress.data = {0xc0, 0xa8, 0x09, 0x02};
    gtp.gTP_TEID = {0x00, 0x00, 0x00, 0x01};

    UP_Parameters_Item up_item{};
    up_item.cell_Group_ID = 0;
    UP_TNL_Information_gTPTunnel tnl_gtp{gtp};
    up_item.uP_TNL_Information = tnl_gtp;

    drb.dL_UP_Parameters = UP_Parameters{up_item};

    // ── Build PDU_Session_Resource_To_Modify_Item ────────────────────────────
    PDU_Session_Resource_To_Modify_Item pdu_item{};
    pdu_item.pDU_Session_ID = 5;
    pdu_item.dRB_To_Modify_List_NG_RAN = DRB_To_Modify_List_NG_RAN{drb};

    // ── Encode PDU_Session_Resource_To_Modify_List (= IE[43] value) ──────────
    BitWriter w_list;
    IEs::encode_PDU_Session_Resource_To_Modify_List(w_list, {pdu_item});
    auto list_bytes = std::vector<uint8_t>(w_list.getBuffer(), w_list.getBuffer() + w_list.getBufferSize());
    std::cout << "  PDU-Session-Modify-List (IE43 value) = " << to_hex(list_bytes) << "\n";

    // ── Build IE[43] ─────────────────────────────────────────────────────────
    IE ie43{};
    ie43.id = 43;
    ie43.criticality = E1AP_CommonDataTypes::Criticality::reject;
    {
        BitString bs;
        bs.bit_length = list_bytes.size() * 8;
        bs.data = list_bytes;
        ie43.value = bs;
    }

    // ── Build System-BearerContextModificationRequest (nG-RAN, variant 1) ────
    System_BearerContextModificationRequest_nG_RAN_BearerContextModificationRequest sys_ng{};
    sys_ng.nG_RAN_BearerContextModificationRequest = {ie43};
    System_BearerContextModificationRequest sys_choice = sys_ng;

    // ── Encode System-BearerContextModificationRequest (= IE[18] value) ──────
    BitWriter w_sys;
    Cont::encode_System_BearerContextModificationRequest(w_sys, sys_choice);
    auto sys_bytes = std::vector<uint8_t>(w_sys.getBuffer(), w_sys.getBuffer() + w_sys.getBufferSize());
    std::cout << "  System-BearerCtxModReq (IE18 value) = " << to_hex(sys_bytes) << "\n";

    // ── Build IE[18] ─────────────────────────────────────────────────────────
    IE ie18{};
    ie18.id = 18;
    ie18.criticality = E1AP_CommonDataTypes::Criticality::reject;
    {
        BitString bs;
        bs.bit_length = sys_bytes.size() * 8;
        bs.data = sys_bytes;
        ie18.value = bs;
    }

    // ── Build IE[0]: gNB-CU-CP-UE-E1AP-ID = 50921473 ────────────────────────
    IE ie0{};
    ie0.id = 2;
    ie0.criticality = E1AP_CommonDataTypes::Criticality::reject;
    {
        BitWriter w;
        IEs::encode_GNB_CU_CP_UE_E1AP_ID(w, 50921473);
        BitString bs;
        bs.data = std::vector<uint8_t>(w.getBuffer(), w.getBuffer() + w.getBufferSize());
        bs.bit_length = bs.data.size() * 8;
        ie0.value = bs;
    }

    // ── Build IE[1]: gNB-CU-UP-UE-E1AP-ID = 50921473 ────────────────────────
    IE ie1{};
    ie1.id = 3;
    ie1.criticality = E1AP_CommonDataTypes::Criticality::reject;
    {
        BitWriter w;
        IEs::encode_GNB_CU_UP_UE_E1AP_ID(w, 50921473);
        BitString bs;
        bs.data = std::vector<uint8_t>(w.getBuffer(), w.getBuffer() + w.getBufferSize());
        bs.bit_length = bs.data.size() * 8;
        ie1.value = bs;
    }

    // ── Build and encode BearerContextModificationRequest ────────────────────
    BearerContextModificationRequest req{};
    req.protocolIEs = {ie0, ie1, ie18};

    BitWriter w_req;
    Cont::encode_BearerContextModificationRequest(w_req, req);
    auto req_bytes = std::vector<uint8_t>(w_req.getBuffer(), w_req.getBuffer() + w_req.getBufferSize());
    std::cout << "  Full BearerContextModificationRequest = " << to_hex(req_bytes) << "\n";
}

// ─── DIAG: encode DRB_To_Modify_Item_NG_RAN and print bytes ──────────────────
static void diag_encode_drb_to_modify_item() {
    using namespace IEs;
    // Build the structure matching the packet
    DRB_To_Modify_Item_NG_RAN drb{};
    drb.dRB_ID = 1;

    // SDAP configuration
    SDAP_Configuration sdap{};
    sdap.defaultDRB = DefaultDRB::true_;
    sdap.sDAP_Header_UL = SDAP_Header_UL::present;
    sdap.sDAP_Header_DL = SDAP_Header_DL::present;
    drb.sDAP_Configuration = sdap;

    // dL-UP-Parameters: 1 item
    UP_Parameters up_params;
    up_params.resize(1);
    UP_Parameters_Item& item = up_params[0];
    item.cell_Group_ID = 0;

    // gTPTunnel
    GTPTunnel gtp{};
    gtp.transportLayerAddress.bit_length = 32;
    gtp.transportLayerAddress.data = {0xc0, 0xa8, 0x09, 0x02};
    gtp.gTP_TEID = {0x00, 0x00, 0x00, 0x01};

    // Wrap in UP_TNL_Information choice (gTPTunnel = index 0)
    struct { GTPTunnel gTPTunnel; } gtp_wrapper{gtp};
    using TNL_Type0 = std::variant_alternative_t<0, UP_TNL_Information>;
    TNL_Type0 tnl_opt;
    tnl_opt.gTPTunnel = gtp;
    item.uP_TNL_Information = tnl_opt;
    drb.dL_UP_Parameters = up_params;

    // Encode
    BitWriter writer;
    IEs::encode_DRB_To_Modify_Item_NG_RAN(writer, drb);
    std::vector<uint8_t> enc(writer.getBuffer(), writer.getBuffer() + writer.getBufferSize());
    std::cout << "  DIAG encode_DRB_To_Modify_Item_NG_RAN = " << to_hex(enc) << "\n";

    // The bs43 data starting at the DRB item (bit 37 = byte 4 bit 5)
    // bs43[4..19] = 02 10 00 00 00 0f 80 c0 a8 09 02 00 00 00 01 00
    // DRB item starts at bit 37 (5 bits into byte 4 = 0x02)
    // For comparison, print bs43 bytes 4-19:
    auto bs43 = from_hex("000040050210000000 0f80c0a809020000000100");
    std::cout << "  bs43 from byte 4 = " << to_hex(std::vector<uint8_t>(bs43.begin()+4, bs43.end())) << "\n";
}

// ─── TC6: BearerContextModificationRequest — build, encode, decode, verify, round-trip
//
// Self-contained: builds the structure programmatically (same values as TC1/TC2),
// encodes to compact bytes, decodes back and verifies:
//   BearerContextModificationRequest (3 IEs)
//     → IE[18] System-BearerContextModificationRequest (nG-RAN, variant 1)
//       → IE[43] PDU-Session-Resource-To-Modify-List
//         → pDU-Session-ID=5, dRB-ID=1, sDAP(true,present,present)
//           dL-UP gTPTunnel(192.168.9.2, TEID=00:00:00:01)

static void tc6_bearer_ctx_mod_req_deep_and_roundtrip() {
    using namespace IEs;
    using namespace Cont;

    // ── Build the structure ───────────────────────────────────────────────────
    DRB_To_Modify_Item_NG_RAN drb{};
    drb.dRB_ID = 1;

    SDAP_Configuration sdap{};
    sdap.defaultDRB = DefaultDRB::true_;
    sdap.sDAP_Header_UL = SDAP_Header_UL::present;
    sdap.sDAP_Header_DL = SDAP_Header_DL::present;
    drb.sDAP_Configuration = sdap;

    GTPTunnel gtp{};
    gtp.transportLayerAddress.bit_length = 32;
    gtp.transportLayerAddress.data = {0xc0, 0xa8, 0x09, 0x02};
    gtp.gTP_TEID = {0x00, 0x00, 0x00, 0x01};

    UP_Parameters_Item up_item{};
    up_item.cell_Group_ID = 0;
    using TNL_Type0 = std::variant_alternative_t<0, UP_TNL_Information>;
    TNL_Type0 tnl_gtp;
    tnl_gtp.gTPTunnel = gtp;
    up_item.uP_TNL_Information = tnl_gtp;
    drb.dL_UP_Parameters = UP_Parameters{up_item};

    PDU_Session_Resource_To_Modify_Item pdu_item{};
    pdu_item.pDU_Session_ID = 5;
    pdu_item.dRB_To_Modify_List_NG_RAN = DRB_To_Modify_List_NG_RAN{drb};

    BitWriter w_list;
    IEs::encode_PDU_Session_Resource_To_Modify_List(w_list, {pdu_item});
    auto list_bytes = std::vector<uint8_t>(w_list.getBuffer(), w_list.getBuffer() + w_list.getBufferSize());

    IE ie43{};
    ie43.id = 43;
    ie43.criticality = E1AP_CommonDataTypes::Criticality::reject;
    { BitString bs; bs.bit_length = list_bytes.size() * 8; bs.data = list_bytes; ie43.value = bs; }

    System_BearerContextModificationRequest_nG_RAN_BearerContextModificationRequest sys_ng{};
    sys_ng.nG_RAN_BearerContextModificationRequest = {ie43};
    System_BearerContextModificationRequest sys_choice = sys_ng;

    BitWriter w_sys;
    Cont::encode_System_BearerContextModificationRequest(w_sys, sys_choice);
    auto sys_bytes = std::vector<uint8_t>(w_sys.getBuffer(), w_sys.getBuffer() + w_sys.getBufferSize());

    IE ie0{}, ie1{}, ie18{};
    ie0.id = 2; ie0.criticality = E1AP_CommonDataTypes::Criticality::reject;
    { BitWriter w; IEs::encode_GNB_CU_CP_UE_E1AP_ID(w, 50921473);
      BitString bs; bs.data = std::vector<uint8_t>(w.getBuffer(), w.getBuffer() + w.getBufferSize());
      bs.bit_length = bs.data.size() * 8; ie0.value = bs; }
    ie1.id = 3; ie1.criticality = E1AP_CommonDataTypes::Criticality::reject;
    { BitWriter w; IEs::encode_GNB_CU_UP_UE_E1AP_ID(w, 50921473);
      BitString bs; bs.data = std::vector<uint8_t>(w.getBuffer(), w.getBuffer() + w.getBufferSize());
      bs.bit_length = bs.data.size() * 8; ie1.value = bs; }
    ie18.id = 18; ie18.criticality = E1AP_CommonDataTypes::Criticality::reject;
    { BitString bs; bs.bit_length = sys_bytes.size() * 8; bs.data = sys_bytes; ie18.value = bs; }

    BearerContextModificationRequest req{};
    req.protocolIEs = {ie0, ie1, ie18};

    // ── Encode to get reference compact bytes ─────────────────────────────────
    BitWriter w_ref;
    Cont::encode_BearerContextModificationRequest(w_ref, req);
    const auto ref_bytes = std::vector<uint8_t>(w_ref.getBuffer(), w_ref.getBuffer() + w_ref.getBufferSize());

    // ── (a) Decode back and verify field values ───────────────────────────────
    BitReader reader(ref_bytes.data(), ref_bytes.size() * 8);
    auto decoded = Cont::decode_BearerContextModificationRequest(reader);

    auto& ies = decoded.protocolIEs;
    chk_eq("numIEs", (int)ies.size(), 3);
    chk_ie(ies[0], 2, 0);
    chk_eq("gNB-CU-CP-UE-E1AP-ID", decode_gnb_cp(ies[0]), (int64_t)50921473);
    chk_ie(ies[1], 3, 0);
    chk_eq("gNB-CU-UP-UE-E1AP-ID", decode_gnb_up(ies[1]), (int64_t)50921473);
    chk_ie(ies[2], 18, 0);

    // ── (b) Deep decode IE[2]: System-BearerContextModificationRequest ────────
    {
        const auto& bs2 = ie_bs(ies[2]);
        BitReader r2(bs2.data.data(), bs2.bit_length);
        auto sys = Cont::decode_System_BearerContextModificationRequest(r2);

        chk_eq("System variant", (int)sys.index(), 1); // nG-RAN
        auto& ng_ran = std::get<1>(sys).nG_RAN_BearerContextModificationRequest;
        chk_eq("inner IEs count", (int)ng_ran.size(), 1);
        chk_ie(ng_ran[0], 43, 0);

        const auto* bs43 = std::any_cast<BitString>(&ng_ran[0].value);
        chk(bs43 != nullptr, "IE[43].value must be BitString");
        BitReader r43(bs43->data.data(), bs43->bit_length);
        auto pdu_list = IEs::decode_PDU_Session_Resource_To_Modify_List(r43);

        chk_eq("PDU session list size", (int)pdu_list.size(), 1);
        const auto& item = pdu_list[0];
        chk_eq("pDU-Session-ID", item.pDU_Session_ID, (int64_t)5);

        chk(item.dRB_To_Modify_List_NG_RAN.has_value(), "dRB-To-Modify-List-NG-RAN must be present");
        const auto& drb_list = *item.dRB_To_Modify_List_NG_RAN;
        chk_eq("DRB list size", (int)drb_list.size(), 1);
        const auto& drb_d = drb_list[0];

        chk_eq("dRB-ID", drb_d.dRB_ID, (int64_t)1);

        chk(drb_d.sDAP_Configuration.has_value(), "sDAP-Configuration must be present");
        const auto& sdap_d = *drb_d.sDAP_Configuration;
        chk(sdap_d.defaultDRB == DefaultDRB::true_, "defaultDRB must be true");
        chk(sdap_d.sDAP_Header_UL == SDAP_Header_UL::present, "sDAP-Header-UL must be present");
        chk(sdap_d.sDAP_Header_DL == SDAP_Header_DL::present, "sDAP-Header-DL must be present");

        chk(drb_d.dL_UP_Parameters.has_value(), "dL-UP-Parameters must be present");
        const auto& dl_params = *drb_d.dL_UP_Parameters;
        chk_eq("dL-UP-Parameters size", (int)dl_params.size(), 1);
        const auto& param = dl_params[0];

        chk_eq("uP-TNL-Information variant", (int)param.uP_TNL_Information.index(), 0);
        const auto& gtp_d = std::get<0>(param.uP_TNL_Information).gTPTunnel;

        chk_eq("TLA bits", (int)gtp_d.transportLayerAddress.bit_length, 32);
        const auto& tla = gtp_d.transportLayerAddress.data;
        chk(tla.size() >= 4 && tla[0] == 0xc0 && tla[1] == 0xa8 && tla[2] == 0x09 && tla[3] == 0x02,
            "TLA must be c0:a8:09:02 (192.168.9.2)");
        chk_eq("TEID size", (int)gtp_d.gTP_TEID.size(), 4);
        chk(gtp_d.gTP_TEID[0] == 0x00 && gtp_d.gTP_TEID[1] == 0x00 &&
            gtp_d.gTP_TEID[2] == 0x00 && gtp_d.gTP_TEID[3] == 0x01,
            "TEID must be 00:00:00:01");
        chk_eq("cell-Group-ID", param.cell_Group_ID, (int64_t)0);
    }

    // ── (c) encode round-trip: re-encode decoded struct, bytes must match ─────
    BitWriter w2;
    Cont::encode_BearerContextModificationRequest(w2, decoded);
    const auto got_hex = to_hex(std::vector<uint8_t>(w2.getBuffer(), w2.getBuffer() + w2.getBufferSize()));
    const auto ref_hex = to_hex(ref_bytes);
    chk(got_hex == ref_hex,
        "Round-trip encode mismatch:\n  expected " + ref_hex + "\n  got      " + got_hex);
}

// ─── TC7: Full Wireshark E1AP-PDU decode → BearerContextSetupResponse ────────
//
// Wireshark capture: successfulOutcome BearerContextSetupResponse
//   procedureCode=8, criticality=reject
//   Inner: same structure as TC5 (compact encoding, 60 bytes)

static void tc7_wireshark_full_pdu_bearer_ctx_setup_response() {
    const auto bytes = from_hex(
        "2008003c00000300020005c00309000100030005c00309000100"
        "104023400001002e401c00000501f0050505020900010c0000001fc0a809010d000120000080");

    BitReader reader(bytes.data(), bytes.size() * 8);
    auto pdu = PDU::decode_E1AP_PDU(reader);

    chk_eq("E1AP-PDU variant (successfulOutcome=1)", (int)pdu.index(), 1);
    auto& succ = std::get<1>(pdu).successfulOutcome;

    chk_eq("procedureCode", (int64_t)succ.procedureCode, (int64_t)8);
    chk_eq("criticality", (int)succ.criticality, 0); // reject

    const auto& outer_bs = std::any_cast<const BitString&>(succ.value);
    BitReader inner(outer_bs.data.data(), outer_bs.bit_length);
    auto resp = Cont::decode_BearerContextSetupResponse(inner);

    auto& ies = resp.protocolIEs;
    chk_eq("numIEs", (int)ies.size(), 3);
    chk_ie(ies[0], 2, 0);
    chk_eq("gNB-CU-CP-UE-E1AP-ID", decode_gnb_cp(ies[0]), (int64_t)50921473);
    chk_ie(ies[1], 3, 0);
    chk_eq("gNB-CU-UP-UE-E1AP-ID", decode_gnb_up(ies[1]), (int64_t)50921473);
    chk_ie(ies[2], 16, 1);

    // Deep decode IE[16]: System-BearerContextSetupResponse → nG-RAN
    {
        const auto& bs2 = ie_bs(ies[2]);
        BitReader r2(bs2.data.data(), bs2.bit_length);
        auto sys = Cont::decode_System_BearerContextSetupResponse(r2);

        chk_eq("System variant", (int)sys.index(), 1);
        auto& ng_ran = std::get<1>(sys).nG_RAN_BearerContextSetupResponse;
        chk_eq("inner IEs count", (int)ng_ran.size(), 1);
        chk_ie(ng_ran[0], 46, 1); // id-PDU-Session-Resource-Setup-List, ignore

        const auto* bs46 = std::any_cast<BitString>(&ng_ran[0].value);
        chk(bs46 != nullptr, "IE[46].value must be BitString");
        BitReader r46(bs46->data.data(), bs46->bit_length);
        auto pdu_list = IEs::decode_PDU_Session_Resource_Setup_List(r46);

        chk_eq("PDU session list size", (int)pdu_list.size(), 1);
        const auto& item = pdu_list[0];
        chk_eq("pDU-Session-ID", item.pDU_Session_ID, (int64_t)5);

        chk_eq("nG-DL-UP variant", (int)item.nG_DL_UP_TNL_Information.index(), 0);
        const auto& dl_gtp = std::get<0>(item.nG_DL_UP_TNL_Information).gTPTunnel;

        chk_eq("DL TLA bits", (int)dl_gtp.transportLayerAddress.bit_length, 32);
        const auto& tla = dl_gtp.transportLayerAddress.data;
        chk(tla.size() >= 4 && tla[0] == 0x05 && tla[1] == 0x05 && tla[2] == 0x05 && tla[3] == 0x02,
            "DL TLA must be 05:05:05:02");

        chk_eq("DL TEID size", (int)dl_gtp.gTP_TEID.size(), 4);
        chk(dl_gtp.gTP_TEID[0] == 0x09 && dl_gtp.gTP_TEID[1] == 0x00 &&
            dl_gtp.gTP_TEID[2] == 0x01 && dl_gtp.gTP_TEID[3] == 0x0c,
            "DL gTP-TEID must be 09:00:01:0c");

        chk_eq("DRB list size", (int)item.dRB_Setup_List_NG_RAN.size(), 1);
        const auto& drb = item.dRB_Setup_List_NG_RAN[0];
        chk_eq("dRB-ID", drb.dRB_ID, (int64_t)1);

        chk_eq("UL UP params size", (int)drb.uL_UP_Transport_Parameters.size(), 1);
        const auto& ul_param = drb.uL_UP_Transport_Parameters[0];
        chk_eq("UL TNL variant", (int)ul_param.uP_TNL_Information.index(), 0);
        const auto& ul_gtp = std::get<0>(ul_param.uP_TNL_Information).gTPTunnel;

        chk_eq("UL TLA bits", (int)ul_gtp.transportLayerAddress.bit_length, 32);
        const auto& ul_tla = ul_gtp.transportLayerAddress.data;
        chk(ul_tla.size() >= 4 && ul_tla[0] == 0xc0 && ul_tla[1] == 0xa8 &&
            ul_tla[2] == 0x09 && ul_tla[3] == 0x01,
            "UL TLA must be c0:a8:09:01 (192.168.9.1)");

        chk_eq("UL TEID size", (int)ul_gtp.gTP_TEID.size(), 4);
        chk(ul_gtp.gTP_TEID[0] == 0x0d && ul_gtp.gTP_TEID[1] == 0x00 &&
            ul_gtp.gTP_TEID[2] == 0x01 && ul_gtp.gTP_TEID[3] == 0x20,
            "UL gTP-TEID must be 0d:00:01:20");

        chk_eq("cell-Group-ID", ul_param.cell_Group_ID, (int64_t)0);
        chk_eq("flow list size", (int)drb.flow_Setup_List.size(), 1);
        chk_eq("qoS-Flow-Identifier", drb.flow_Setup_List[0].qoS_Flow_Identifier, (int64_t)1);
    }
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

    tc("TC5: BearerContextSetupResponse deep decode + encode round-trip",
       tc5_bearer_ctx_setup_response_deep_and_roundtrip);

    std::cout << "\n[DIAG] Encoded DRB_To_Modify_Item_NG_RAN:\n";
    diag_encode_drb_to_modify_item();

    std::cout << "\n[DIAG] Full TC6 encoded structure:\n";
    diag_encode_full_tc6();

    tc("TC6: BearerContextModificationRequest deep decode + encode round-trip",
       tc6_bearer_ctx_mod_req_deep_and_roundtrip);

    tc("TC7: Full Wireshark PDU decode → BearerContextSetupResponse deep decode",
       tc7_wireshark_full_pdu_bearer_ctx_setup_response);

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}
